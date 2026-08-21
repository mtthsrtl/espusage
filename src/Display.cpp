#include "Display.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <lvgl.h>

static Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
static Arduino_ESP32RGBPanel *rgb = new Arduino_ESP32RGBPanel(
  18,17,16,21, 11,12,13,14,0, 8,20,3,46,9,10, 4,5,6,7,15,
  1,10,8,50, 1,10,8,20, 0,12000000,true,0,0,0);
static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(480,480,rgb,0,true,bus,GFX_NOT_DEFINED,
  st7701_type1_init_operations,sizeof(st7701_type1_init_operations));
static TAMC_GT911 touch(19,45,-1,-1,480,480);
static lv_disp_draw_buf_t drawBuf; static lv_color_t *buf;
static lv_obj_t *networkLabel, *titleLabel, *cards[4], *values[4], *bars[4], *resetLabels[4];
static UsageSnapshot snapshots[2];

static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *p) {
  gfx->draw16bitRGBBitmap(a->x1,a->y1,(uint16_t*)&p->full,a->x2-a->x1+1,a->y2-a->y1+1); lv_disp_flush_ready(d);
}
static void readTouch(lv_indev_drv_t*,lv_indev_data_t *data) {
  touch.read(); if (touch.isTouched && touch.touches) { data->state=LV_INDEV_STATE_PR; data->point.x=479-touch.points[0].x; data->point.y=479-touch.points[0].y; }
  else data->state=LV_INDEV_STATE_REL;
}
static lv_obj_t *label(lv_obj_t *p,const char *text,const lv_font_t *font,lv_color_t color) {
  lv_obj_t *o=lv_label_create(p); lv_label_set_text(o,text); lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,color,0); return o;
}
static void styleBase(lv_obj_t *o, lv_color_t bg) { lv_obj_set_style_bg_color(o,bg,0); lv_obj_set_style_border_width(o,0,0); lv_obj_set_style_radius(o,16,0); }
static void cardEvent(lv_event_t *e) {
  intptr_t index=(intptr_t)lv_event_get_user_data(e); UsageSnapshot &s=snapshots[index/2];
  lv_obj_t *modal=lv_obj_create(lv_scr_act()); lv_obj_set_size(modal,430,390); lv_obj_center(modal); styleBase(modal,lv_color_hex(0x141922));
  lv_obj_set_style_border_width(modal,1,0); lv_obj_set_style_border_color(modal,lv_color_hex(0x2A3442),0);
  lv_obj_t *t=label(modal,(s.provider+" details").c_str(),&lv_font_montserrat_14,lv_color_hex(0xF2F5F8)); lv_obj_align(t,LV_ALIGN_TOP_LEFT,8,8);
  String body="Status: "+s.status+"\nPlan: "+(s.plan.length()?s.plan:"—")+"\n\n"+s.primary.label+": "+String(s.primary.usedPercent,1)+"%\n"+s.primary.resetText+"\n\n"+s.secondary.label+": "+String(s.secondary.usedPercent,1)+"%\n"+s.secondary.resetText;
  lv_obj_t *b=label(modal,body.c_str(),&lv_font_montserrat_14,lv_color_hex(0xAEB8C4)); lv_obj_set_width(b,390); lv_obj_align(b,LV_ALIGN_TOP_LEFT,8,58);
  lv_obj_t *close=lv_btn_create(modal); lv_obj_set_size(close,120,48); lv_obj_align(close,LV_ALIGN_BOTTOM_RIGHT,-4,-4); styleBase(close,lv_color_hex(0x263142));
  lv_obj_t *cl=label(close,"CLOSE",&lv_font_montserrat_14,lv_color_hex(0xE7EDF4)); lv_obj_center(cl);
  lv_obj_add_event_cb(close,[](lv_event_t *ev){lv_obj_del((lv_obj_t*)lv_event_get_user_data(ev));},LV_EVENT_CLICKED,modal);
}
static void makeCard(int i,int y,const char *provider,const char *window,lv_color_t accent) {
  lv_obj_t *c=lv_obj_create(lv_scr_act()); cards[i]=c; lv_obj_set_size(c,440,150); lv_obj_set_pos(c,20,y); styleBase(c,lv_color_hex(0x151A22));
  lv_obj_set_style_border_width(c,1,0); lv_obj_set_style_border_color(c,lv_color_hex(0x242C38),0); lv_obj_clear_flag(c,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(c,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c,cardEvent,LV_EVENT_CLICKED,(void*)(intptr_t)i);
  lv_obj_t *p=label(c,provider,&lv_font_montserrat_14,accent); lv_obj_align(p,LV_ALIGN_TOP_LEFT,2,-2);
  lv_obj_t *w=label(c,window,&lv_font_montserrat_14,lv_color_hex(0x7F8A98)); lv_obj_align(w,LV_ALIGN_TOP_LEFT,2,25);
  values[i]=label(c,"--%",&lv_font_montserrat_14,lv_color_hex(0xF1F4F8)); lv_obj_align(values[i],LV_ALIGN_TOP_RIGHT,-2,-4);
  bars[i]=lv_bar_create(c); lv_obj_set_size(bars[i],402,10); lv_obj_align(bars[i],LV_ALIGN_BOTTOM_MID,0,-26); lv_bar_set_range(bars[i],0,100);
  lv_obj_set_style_bg_color(bars[i],lv_color_hex(0x252C36),LV_PART_MAIN); lv_obj_set_style_bg_color(bars[i],accent,LV_PART_INDICATOR); lv_obj_set_style_radius(bars[i],5,LV_PART_MAIN);
  resetLabels[i]=label(c,"Not configured",&lv_font_montserrat_14,lv_color_hex(0x818C99)); lv_obj_align(resetLabels[i],LV_ALIGN_BOTTOM_LEFT,2,2);
}
void displayBegin() {
  pinMode(38,OUTPUT); analogWriteFrequency(150); displaySetBrightness(85); gfx->begin(12000000); gfx->fillScreen(BLACK); touch.begin(); touch.setRotation(ROTATION_NORMAL);
  lv_init(); buf=(lv_color_t*)heap_caps_malloc(480*32*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); lv_disp_draw_buf_init(&drawBuf,buf,nullptr,480*32);
  static lv_disp_drv_t dd; lv_disp_drv_init(&dd); dd.hor_res=480; dd.ver_res=480; dd.flush_cb=flush; dd.draw_buf=&drawBuf; lv_disp_drv_register(&dd);
  static lv_indev_drv_t id; lv_indev_drv_init(&id); id.type=LV_INDEV_TYPE_POINTER; id.read_cb=readTouch; lv_indev_drv_register(&id);
  lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(0x0B0E13),0); lv_obj_set_style_bg_opa(lv_scr_act(),LV_OPA_COVER,0);
  titleLabel=label(lv_scr_act(),"DEV USAGE",&lv_font_montserrat_14,lv_color_hex(0xF3F6F9)); lv_obj_set_pos(titleLabel,22,18);
  networkLabel=label(lv_scr_act(),"STARTING",&lv_font_montserrat_14,lv_color_hex(0xF5A524)); lv_obj_align(networkLabel,LV_ALIGN_TOP_RIGHT,-22,23);
  makeCard(0,60,"CODEX","PRIMARY",lv_color_hex(0x10A37F)); makeCard(2,230,"CURSOR","MONTH",lv_color_hex(0x8B5CF6));
  lv_obj_t *hint=label(lv_scr_act(),"Tap a card for details  •  Configure at espusage.local",&lv_font_montserrat_14,lv_color_hex(0x5F6976)); lv_obj_align(hint,LV_ALIGN_BOTTOM_MID,0,-14);
}
void displayLoop(){lv_timer_handler();}
void displaySetBrightness(uint8_t v){analogWrite(38,map(constrain(v,0,100),0,100,0,255));}
void displaySetNetwork(const String &s,bool ok){if(!networkLabel)return;lv_label_set_text(networkLabel,s.c_str());lv_obj_set_style_text_color(networkLabel,lv_color_hex(ok?0x49D69A:0xF5A524),0);}
void displayUpdate(const UsageSnapshot &c,const UsageSnapshot &u){snapshots[0]=c;snapshots[1]=u;UsageSnapshot a[2]={c,u};for(int j=0;j<2;j++){int i=j*2;float p=a[j].primary.usedPercent;lv_label_set_text(values[i],p<0?"--%":(String(p,0)+"%").c_str());lv_bar_set_value(bars[i],p<0?0:(int)p,LV_ANIM_ON);lv_label_set_text(resetLabels[i],(a[j].primary.resetText.length()?a[j].primary.resetText:a[j].status).c_str());}}
