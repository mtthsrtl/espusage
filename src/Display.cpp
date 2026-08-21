#include "Display.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <lvgl.h>

static Arduino_DataBus *bus=new Arduino_SWSPI(GFX_NOT_DEFINED,39,48,47,GFX_NOT_DEFINED);
static Arduino_ESP32RGBPanel *rgb=new Arduino_ESP32RGBPanel(18,17,16,21,11,12,13,14,0,8,20,3,46,9,10,4,5,6,7,15,1,10,8,50,1,10,8,20,0,12000000,true,0,0,0);
static Arduino_RGB_Display *gfx=new Arduino_RGB_Display(480,480,rgb,0,true,bus,GFX_NOT_DEFINED,st7701_type1_init_operations,sizeof(st7701_type1_init_operations));
static TAMC_GT911 touch(19,45,41,42,480,480);
static lv_disp_draw_buf_t drawBuf; static lv_color_t *drawMemory;
static lv_obj_t *networkLabel,*cards[2],*providerLabels[2],*windowLabels[2],*values[2],*statusLabels[2],*bars[2],*resetLabels[2];
static UsageSnapshot snapshots[2];
static String networkAddress;

static lv_color_t C(uint32_t value){return lv_color_hex(value);}
static lv_obj_t *makeLabel(lv_obj_t *parent,const char *text,const lv_font_t *font,lv_color_t color){lv_obj_t *o=lv_label_create(parent);lv_label_set_text(o,text);lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,color,0);return o;}
static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *p){gfx->draw16bitRGBBitmap(a->x1,a->y1,(uint16_t*)&p->full,a->x2-a->x1+1,a->y2-a->y1+1);lv_disp_flush_ready(d);}
static void readTouch(lv_indev_drv_t*,lv_indev_data_t *data){touch.read();if(touch.isTouched&&touch.touches){data->state=LV_INDEV_STATE_PR;data->point.x=479-touch.points[0].x;data->point.y=479-touch.points[0].y;}else data->state=LV_INDEV_STATE_REL;}
static void cardStyle(lv_obj_t *o){lv_obj_set_style_bg_color(o,C(0x131820),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,C(0x2A323D),0);lv_obj_set_style_radius(o,18,0);lv_obj_set_style_pad_all(o,18,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);}

static void closeModal(lv_event_t *e){lv_obj_del((lv_obj_t*)lv_event_get_user_data(e));}
static void openDetails(lv_event_t *e){
  int i=(int)(intptr_t)lv_event_get_user_data(e);UsageSnapshot &s=snapshots[i];
  lv_obj_t *m=lv_obj_create(lv_scr_act());lv_obj_set_size(m,430,390);lv_obj_center(m);cardStyle(m);lv_obj_set_style_bg_color(m,C(0x11161D),0);
  lv_obj_t *t=makeLabel(m,(s.provider+" DETAILS").c_str(),&lv_font_montserrat_24,C(0xF4F7FA));lv_obj_align(t,LV_ALIGN_TOP_LEFT,2,2);
  String body="Status  "+s.status+"\nPlan  "+(s.plan.length()?s.plan:"—")+"\n\n"+s.primary.label+"  "+(s.primary.usedPercent<0?"—":String(s.primary.usedPercent,1)+"%")+"\n"+s.primary.resetText+"\n\n"+s.secondary.label+"  "+(s.secondary.usedPercent<0?"—":String(s.secondary.usedPercent,1)+"%")+"\n"+s.secondary.resetText;
  lv_obj_t *b=makeLabel(m,body.c_str(),&lv_font_montserrat_16,C(0xAEB8C5));lv_obj_set_width(b,390);lv_obj_align(b,LV_ALIGN_TOP_LEFT,2,58);
  lv_obj_t *x=lv_btn_create(m);lv_obj_set_size(x,120,48);lv_obj_align(x,LV_ALIGN_BOTTOM_RIGHT,0,0);lv_obj_set_style_bg_color(x,C(0x263140),0);lv_obj_set_style_radius(x,12,0);lv_obj_t *xl=makeLabel(x,"CLOSE",&lv_font_montserrat_14,C(0xEEF2F6));lv_obj_center(xl);lv_obj_add_event_cb(x,closeModal,LV_EVENT_CLICKED,m);
}
static void makeUsageCard(int i,int y,const char *provider,const char *window){
  cards[i]=lv_obj_create(lv_scr_act());lv_obj_set_size(cards[i],440,164);lv_obj_set_pos(cards[i],20,y);cardStyle(cards[i]);lv_obj_add_flag(cards[i],LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(cards[i],openDetails,LV_EVENT_CLICKED,(void*)(intptr_t)i);
  providerLabels[i]=makeLabel(cards[i],provider,&lv_font_montserrat_20,C(0xF0F4F8));lv_obj_align(providerLabels[i],LV_ALIGN_TOP_LEFT,0,-2);
  statusLabels[i]=makeLabel(cards[i],"WAITING",&lv_font_montserrat_12,C(0x8D98A6));lv_obj_align(statusLabels[i],LV_ALIGN_TOP_RIGHT,0,3);
  windowLabels[i]=makeLabel(cards[i],window,&lv_font_montserrat_12,C(0x7C8795));lv_obj_align(windowLabels[i],LV_ALIGN_TOP_LEFT,0,32);
  values[i]=makeLabel(cards[i],"--%",&lv_font_montserrat_32,C(0xF4F7FA));lv_obj_align(values[i],LV_ALIGN_TOP_RIGHT,0,27);
  bars[i]=lv_bar_create(cards[i]);lv_obj_set_size(bars[i],402,12);lv_obj_align(bars[i],LV_ALIGN_BOTTOM_MID,0,-29);lv_bar_set_range(bars[i],0,100);lv_obj_set_style_bg_color(bars[i],C(0x252C36),LV_PART_MAIN);lv_obj_set_style_bg_opa(bars[i],LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_radius(bars[i],6,LV_PART_MAIN);lv_obj_set_style_radius(bars[i],6,LV_PART_INDICATOR);
  resetLabels[i]=makeLabel(cards[i],"Waiting for usage data",&lv_font_montserrat_12,C(0x8893A0));lv_obj_align(resetLabels[i],LV_ALIGN_BOTTOM_LEFT,0,1);
}
void displayBegin(){
  pinMode(38,OUTPUT);digitalWrite(38,HIGH);gfx->begin(12000000);gfx->fillScreen(BLACK);touch.begin();touch.setRotation(ROTATION_NORMAL);
  lv_init();drawMemory=(lv_color_t*)heap_caps_malloc(480*32*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);lv_disp_draw_buf_init(&drawBuf,drawMemory,nullptr,480*32);
  static lv_disp_drv_t dd;lv_disp_drv_init(&dd);dd.hor_res=480;dd.ver_res=480;dd.flush_cb=flush;dd.draw_buf=&drawBuf;lv_disp_drv_register(&dd);
  static lv_indev_drv_t id;lv_indev_drv_init(&id);id.type=LV_INDEV_TYPE_POINTER;id.read_cb=readTouch;lv_indev_drv_register(&id);
  lv_obj_set_style_bg_color(lv_scr_act(),C(0x090C11),0);lv_obj_set_style_bg_opa(lv_scr_act(),LV_OPA_COVER,0);
  lv_obj_t *title=makeLabel(lv_scr_act(),"AI USAGE",&lv_font_montserrat_24,C(0xF3F6F9));lv_obj_set_pos(title,22,17);
  networkLabel=makeLabel(lv_scr_act(),"STARTING",&lv_font_montserrat_12,C(0xF2A93B));lv_obj_align(networkLabel,LV_ALIGN_TOP_RIGHT,-22,25);
  makeUsageCard(0,62,"CODEX","PRIMARY WINDOW");makeUsageCard(1,242,"CURSOR","MONTHLY USAGE");
  lv_obj_t *hint=makeLabel(lv_scr_act(),"Tap a card for details  •  Settings: espusage.local",&lv_font_montserrat_12,C(0x596473));lv_obj_align(hint,LV_ALIGN_BOTTOM_MID,0,-12);
}
void displayLoop(){lv_timer_handler();}
void displaySetBrightness(uint8_t value){digitalWrite(38,value?HIGH:LOW);}
void displaySetNetwork(const String &text,bool connected){networkAddress=connected?text:"";if(!networkLabel)return;lv_label_set_text(networkLabel,text.c_str());lv_obj_set_style_text_color(networkLabel,C(connected?0x45D597:0xF2A93B),0);}
void displayUpdate(const UsageSnapshot &codex,const UsageSnapshot &cursor,uint8_t warning,uint8_t critical){
  snapshots[0]=codex;snapshots[1]=cursor;UsageSnapshot data[2]={codex,cursor};
  for(int i=0;i<2;i++){float p=data[i].primary.usedPercent;lv_color_t color;String state;
    if(!data[i].ok||p<0){color=C(0x7D8896);state=data[i].status=="disabled"?"DISABLED":"NO DATA";}
    else if(p>=critical){color=C(0xFF4757);state="CRITICAL";}
    else if(p>=warning){color=C(0xF4A62A);state="WARNING";}
    else{color=C(0x3DDC84);state="OK";}
    lv_label_set_text(statusLabels[i],state.c_str());lv_obj_set_style_text_color(statusLabels[i],color,0);lv_obj_set_style_bg_color(bars[i],color,LV_PART_INDICATOR);
    lv_label_set_text(values[i],p<0?"--%":(String(p,0)+"%").c_str());lv_obj_set_style_text_color(values[i],color,0);lv_bar_set_value(bars[i],p<0?0:(int)constrain(p,0,100),LV_ANIM_ON);
    lv_label_set_text(windowLabels[i],data[i].primary.label.c_str());
    String reset;
    if((data[i].status=="disabled"||data[i].status.indexOf("token missing")>=0)&&networkAddress.length()){
      state="SETUP";lv_label_set_text(statusLabels[i],state.c_str());
      reset="Open http://"+networkAddress;
    }else reset=data[i].primary.resetText.length()?data[i].primary.resetText:data[i].status;
    lv_label_set_text(resetLabels[i],reset.c_str());
  }
}

