#include "Display.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <lvgl.h>

static Arduino_DataBus *bus=new Arduino_SWSPI(GFX_NOT_DEFINED,39,48,47,GFX_NOT_DEFINED);
static Arduino_ESP32RGBPanel *rgb=new Arduino_ESP32RGBPanel(18,17,16,21,11,12,13,14,0,8,20,3,46,9,10,4,5,6,7,15,1,10,8,50,1,10,8,20,0,10000000,false,0,0,0);
static Arduino_RGB_Display *gfx=new Arduino_RGB_Display(480,480,rgb,1,true,bus,GFX_NOT_DEFINED,st7701_type9_init_operations,sizeof(st7701_type9_init_operations));
static TAMC_GT911 touch(19,45,41,42,480,480);
static lv_disp_draw_buf_t drawBuf; static lv_color_t *drawMemory;
static lv_obj_t *networkLabel,*statusLabels[4],*values[4],*bars[4],*resetLabels[4];
static String rowNames[4]={"CODEX WEEKLY","CURSOR MODELS","AUTO MODELS","API USAGE"};
static UsageWindow rowData[4]; static String rowStatus[4]; static String networkAddress;

static lv_color_t C(uint32_t v){return lv_color_hex(v);}
static lv_obj_t *label(lv_obj_t *p,const char *s,const lv_font_t *f,lv_color_t c){lv_obj_t *o=lv_label_create(p);lv_label_set_text(o,s);lv_obj_set_style_text_font(o,f,0);lv_obj_set_style_text_color(o,c,0);return o;}
static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *p){gfx->draw16bitRGBBitmap(a->x1,a->y1,(uint16_t*)&p->full,a->x2-a->x1+1,a->y2-a->y1+1);lv_disp_flush_ready(d);}
static void readTouch(lv_indev_drv_t*,lv_indev_data_t *d){touch.read();if(touch.isTouched&&touch.touches){d->state=LV_INDEV_STATE_PR;d->point.x=479-touch.points[0].x;d->point.y=479-touch.points[0].y;}else d->state=LV_INDEV_STATE_REL;}
static void panel(lv_obj_t *o){lv_obj_set_style_bg_color(o,C(0x101010),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,C(0x303030),0);lv_obj_set_style_radius(o,14,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);}
static void closeModal(lv_event_t *e){lv_obj_del((lv_obj_t*)lv_event_get_user_data(e));}
static void details(lv_event_t *e){
  int i=(int)(intptr_t)lv_event_get_user_data(e);lv_obj_t *m=lv_obj_create(lv_scr_act());lv_obj_set_size(m,430,300);lv_obj_center(m);panel(m);lv_obj_set_style_pad_all(m,22,0);
  lv_obj_t *t=label(m,rowNames[i].c_str(),&lv_font_montserrat_24,C(0xF4F4F4));lv_obj_align(t,LV_ALIGN_TOP_LEFT,0,0);
  String value=rowData[i].usedPercent<0?"No usage data":String(rowData[i].usedPercent,1)+"% used";
  String body=value+"\n\n"+rowData[i].resetText+"\n\nProvider: "+rowStatus[i];
  lv_obj_t *b=label(m,body.c_str(),&lv_font_montserrat_16,C(0xB8B8B8));lv_obj_set_width(b,380);lv_obj_align(b,LV_ALIGN_TOP_LEFT,0,55);
  lv_obj_t *x=lv_btn_create(m);lv_obj_set_size(x,110,44);lv_obj_align(x,LV_ALIGN_BOTTOM_RIGHT,0,0);lv_obj_set_style_bg_color(x,C(0x252525),0);lv_obj_t *xl=label(x,"CLOSE",&lv_font_montserrat_14,C(0xFFFFFF));lv_obj_center(xl);lv_obj_add_event_cb(x,closeModal,LV_EVENT_CLICKED,m);
}
static void makeCard(int i,int y){
  lv_obj_t *c=lv_obj_create(lv_scr_act());lv_obj_set_size(c,440,88);lv_obj_set_pos(c,20,y);panel(c);lv_obj_set_style_pad_all(c,13,0);lv_obj_add_flag(c,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(c,details,LV_EVENT_CLICKED,(void*)(intptr_t)i);
  lv_obj_t *n=label(c,rowNames[i].c_str(),&lv_font_montserrat_16,C(0xF2F2F2));lv_obj_align(n,LV_ALIGN_TOP_LEFT,0,-2);
  statusLabels[i]=label(c,"WAITING",&lv_font_montserrat_12,C(0x888888));lv_obj_align(statusLabels[i],LV_ALIGN_TOP_RIGHT,-68,0);
  values[i]=label(c,"--%",&lv_font_montserrat_20,C(0xF2F2F2));lv_obj_align(values[i],LV_ALIGN_TOP_RIGHT,0,-4);
  bars[i]=lv_bar_create(c);lv_obj_set_size(bars[i],414,9);lv_obj_align(bars[i],LV_ALIGN_BOTTOM_MID,0,-18);lv_bar_set_range(bars[i],0,100);lv_obj_set_style_bg_color(bars[i],C(0x282828),LV_PART_MAIN);lv_obj_set_style_bg_opa(bars[i],LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_radius(bars[i],5,LV_PART_MAIN);lv_obj_set_style_radius(bars[i],5,LV_PART_INDICATOR);
  resetLabels[i]=label(c,"Waiting for usage data",&lv_font_montserrat_12,C(0x929292));lv_obj_align(resetLabels[i],LV_ALIGN_BOTTOM_LEFT,0,1);
}
void displayBegin(){
  pinMode(38,OUTPUT);digitalWrite(38,HIGH);gfx->begin(10000000);gfx->fillScreen(BLACK);touch.begin();touch.setRotation(ROTATION_NORMAL);
  lv_init();drawMemory=(lv_color_t*)heap_caps_malloc(480*32*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);lv_disp_draw_buf_init(&drawBuf,drawMemory,nullptr,480*32);
  static lv_disp_drv_t dd;lv_disp_drv_init(&dd);dd.hor_res=480;dd.ver_res=480;dd.flush_cb=flush;dd.draw_buf=&drawBuf;lv_disp_drv_register(&dd);
  static lv_indev_drv_t id;lv_indev_drv_init(&id);id.type=LV_INDEV_TYPE_POINTER;id.read_cb=readTouch;lv_indev_drv_register(&id);
  lv_obj_set_style_bg_color(lv_scr_act(),C(0x000000),0);lv_obj_set_style_bg_opa(lv_scr_act(),LV_OPA_COVER,0);
  lv_obj_t *title=label(lv_scr_act(),"AI USAGE",&lv_font_montserrat_20,C(0xFFFFFF));lv_obj_set_pos(title,20,15);
  networkLabel=label(lv_scr_act(),"STARTING",&lv_font_montserrat_12,C(0xF2A93B));lv_obj_align(networkLabel,LV_ALIGN_TOP_RIGHT,-20,20);
  for(int i=0;i<4;i++)makeCard(i,50+i*101);
}
void displayLoop(){lv_timer_handler();}
void displaySetBrightness(uint8_t v){digitalWrite(38,v?HIGH:LOW);}
void displaySetNetwork(const String &s,bool connected){networkAddress=connected?s:"";if(!networkLabel)return;lv_label_set_text(networkLabel,s.c_str());lv_obj_set_style_text_color(networkLabel,C(connected?0x45D597:0xF2A93B),0);}
static void updateRow(int i,const UsageWindow &w,const String &providerStatus,uint8_t warning,uint8_t critical){
  rowData[i]=w;rowStatus[i]=providerStatus;float p=w.usedPercent;lv_color_t color;String state;
  if(p<0){color=C(0x7D7D7D);state="NO DATA";}else if(p>=critical){color=C(0xFF4040);state="CRITICAL";}else if(p>=warning){color=C(0xF0A020);state="WARNING";}else{color=C(0x35D078);state="OK";}
  lv_label_set_text(statusLabels[i],state.c_str());lv_obj_set_style_text_color(statusLabels[i],color,0);lv_label_set_text(values[i],p<0?"--%":(String(p,0)+"%").c_str());lv_obj_set_style_text_color(values[i],color,0);lv_obj_set_style_bg_color(bars[i],color,LV_PART_INDICATOR);lv_bar_set_value(bars[i],p<0?0:(int)constrain(p,0,100),LV_ANIM_ON);
  String bottom=w.resetText.length()?w.resetText:providerStatus;if(p<0&&networkAddress.length()&&(providerStatus=="disabled"||providerStatus.indexOf("missing")>=0))bottom="Setup: http://"+networkAddress;lv_label_set_text(resetLabels[i],bottom.c_str());
}
void displayUpdate(const UsageSnapshot &codex,const UsageSnapshot &cursor,uint8_t warning,uint8_t critical){
  updateRow(0,codex.primary,codex.status,warning,critical);updateRow(1,cursor.primary,cursor.status,warning,critical);updateRow(2,cursor.secondary,cursor.status,warning,critical);updateRow(3,cursor.tertiary,cursor.status,warning,critical);
}
