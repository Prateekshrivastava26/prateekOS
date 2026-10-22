#include "gui.h"
#include "keyboard.h"
#include "mouse.h"
#include "ramfs.h"
#include "diskfs.h"
#include "timer.h"
#include "pmm.h"
#include "rtc.h"
#include "ide.h"
#include "host_tools.h"
#include "calculator.h"
#include "wallpaper.h"

#include <stddef.h>
#include <stdint.h>

extern char font8x8_basic[128][8];

#define GUI_MAX_FILES 47
#define GUI_NAME_SIZE 128
#define GUI_TEXT_SIZE 513

typedef struct {
    uint8_t on_disk;
    char name[GUI_NAME_SIZE];
} gui_file_t;

static struct limine_framebuffer *fb;
static uint32_t cursor_under[16][10];
static int32_t cursor_x, cursor_y;
static int cursor_visible;
static uint32_t file_scroll;
static rtc_datetime_t desktop_time;
static int clock_valid;
static uint64_t disk_capacity;
static uint8_t clock_12h;
static uint8_t pointer_speed=1;
static uint8_t wallpaper_mode; /* Custom, Ocean, Sunset, Forest. */
static wallpaper_t wallpaper;
static calculator_t calculator;
static int appearance_loaded;
static const char *appearance_status="Save stores these choices on your disk.";

static void save_appearance(void) {
    char data[8]={'U','I','1',':',(char)('0'+wallpaper_mode),(char)('0'+pointer_speed),(char)('0'+clock_12h),0};
    appearance_status=diskfs_write_file("prateek-ui.cfg",data)?"Appearance saved.":"Save failed: check disk format or free file slots.";
}
static void load_appearance(void) {
    const uint8_t *data=0;uint64_t size=0;
    wallpaper.pixels=0;
    if(ramfs_get_file("wallpaper.bmp",&data,&size)) (void)wallpaper_decode(&wallpaper,data,size);
    if(appearance_loaded)return;
    appearance_loaded=1;uint8_t config[8];size_t count=0;
    if(diskfs_read_file("prateek-ui.cfg",config,sizeof config,&count)&&count==7&&
       config[0]=='U'&&config[1]=='I'&&config[2]=='1'&&config[3]==':'&&
       config[4]>='0'&&config[4]<='3'&&config[5]>='0'&&config[5]<='2'&&config[6]>='0'&&config[6]<='1') {
        wallpaper_mode=config[4]-'0';pointer_speed=config[5]-'0';clock_12h=config[6]-'0';
    }
}

static uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
    if(fb->red_mask_size==8&&fb->green_mask_size==8&&fb->blue_mask_size==8)
        return ((uint32_t)r<<fb->red_mask_shift)|((uint32_t)g<<fb->green_mask_shift)|((uint32_t)b<<fb->blue_mask_shift);
    uint64_t rm = fb->red_mask_size ? (1ULL << fb->red_mask_size) - 1 : 0;
    uint64_t gm = fb->green_mask_size ? (1ULL << fb->green_mask_size) - 1 : 0;
    uint64_t bm = fb->blue_mask_size ? (1ULL << fb->blue_mask_size) - 1 : 0;
    return (uint32_t)(((r * rm / 255) << fb->red_mask_shift) |
                      ((g * gm / 255) << fb->green_mask_shift) |
                      ((b * bm / 255) << fb->blue_mask_shift));
}

static void rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > (int32_t)fb->width ? (int32_t)fb->width : x + w;
    int32_t y1 = y + h > (int32_t)fb->height ? (int32_t)fb->height : y + h;
    volatile uint32_t *pixels = fb->address;
    for (int32_t yy = y0; yy < y1; yy++)
        for (int32_t xx = x0; xx < x1; xx++)
            pixels[(uint64_t)yy * (fb->pitch / 4) + (uint32_t)xx] = c;
}

static void text(int32_t x, int32_t y, const char *s, uint32_t c) {
    while (*s) {
        uint8_t ch = (uint8_t)*s++;
        if (ch >= 128) ch = '?';
        for (uint32_t row = 0; row < 8; row++) {
            uint8_t bits = (uint8_t)font8x8_basic[ch][row];
            for (uint32_t col = 0; col < 8; col++)
                if (bits & (1u << col)) rect(x + (int32_t)col, y + (int32_t)row, 1, 1, c);
        }
        x += 8;
    }
}

static int inside(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static void pointer_restore(void) {
    if (!cursor_visible) return;
    volatile uint32_t *pixels = fb->address;
    uint64_t stride = fb->pitch / 4;
    for (int32_t row = 0; row < 16; row++) {
        for (int32_t col = 0; col < 10; col++) {
            int32_t x = cursor_x + col, y = cursor_y + row;
            if (x >= 0 && y >= 0 && (uint64_t)x < fb->width && (uint64_t)y < fb->height)
                pixels[(uint64_t)y * stride + (uint32_t)x] = cursor_under[row][col];
        }
    }
    cursor_visible = 0;
}

static void pointer_draw(int32_t x, int32_t y) {
    volatile uint32_t *pixels = fb->address;
    uint64_t stride = fb->pitch / 4;
    uint32_t edge = color(12,20,34), fill = color(255,255,255);
    cursor_x = x; cursor_y = y;
    for (int32_t row = 0; row < 16; row++) {
        for (int32_t col = 0; col < 10; col++) {
            int32_t px = x + col, py = y + row;
            if (px < 0 || py < 0 || (uint64_t)px >= fb->width || (uint64_t)py >= fb->height) {
                cursor_under[row][col] = 0;
                continue;
            }
            uint64_t index = (uint64_t)py * stride + (uint32_t)px;
            cursor_under[row][col] = pixels[index];
            if (col <= row / 2) {
                uint32_t c = (row == 0 || col == 0 || col == row / 2) ? edge : fill;
                pixels[index] = c;
            }
        }
    }
    cursor_visible = 1;
}

static void draw_wallpaper(void) {
    volatile uint32_t *pixels=fb->address;uint64_t stride=fb->pitch/4;
    uint32_t height=fb->height>38?(uint32_t)fb->height-38:0;
    if(wallpaper_mode==0&&wallpaper.pixels&&height) {
        rect(0,0,(int32_t)fb->width,(int32_t)height,color(255,255,255));
        uint32_t w=(uint32_t)fb->width,h=(uint32_t)((uint64_t)wallpaper.height*w/wallpaper.width);
        if(h>height){h=height;w=(uint32_t)((uint64_t)wallpaper.width*h/wallpaper.height);}
        if(!w||!h)return;
        uint32_t left=((uint32_t)fb->width-w)/2,top=(height-h)/2;
        for(uint32_t y=0;y<h;y++){
            uint32_t sy=(uint32_t)((uint64_t)y*wallpaper.height/h);
            if(!wallpaper.top_down)sy=wallpaper.height-1-sy;
            const uint8_t *row=wallpaper.pixels+(uint64_t)sy*wallpaper.stride;
            for(uint32_t x=0;x<w;x++){
                uint32_t sx=(uint32_t)((uint64_t)x*wallpaper.width/w);
                const uint8_t *pixel=row+sx*3;
                pixels[(uint64_t)(top+y)*stride+left+x]=color(pixel[2],pixel[1],pixel[0]);
            }
        }
        return;
    }
    for(uint32_t y=0;y<height;y++){
        uint8_t t=(uint8_t)((uint64_t)y*255/height);uint32_t c;
        if(wallpaper_mode==2)c=color((uint8_t)(76+96*t/255),(uint8_t)(30+58*t/255),(uint8_t)(74+22*t/255));
        else if(wallpaper_mode==3)c=color((uint8_t)(12+14*t/255),(uint8_t)(48+65*t/255),(uint8_t)(35+25*t/255));
        else c=color((uint8_t)(12+22*t/255),(uint8_t)(35+50*t/255),(uint8_t)(74+90*t/255));
        for(uint32_t x=0;x<fb->width;x++)pixels[(uint64_t)y*stride+x]=c;
    }
}

static void draw_file_icon(int32_t x, int32_t y, const char *label) {
    rect(x + 7, y, 32, 27, color(247,194,72));
    rect(x, y + 7, 46, 32, color(255,215,96));
    rect(x + 5, y + 13, 36, 20, color(255,226,132));
    uint32_t n=0;while(label[n])n++;
    rect(x-6,y+42,(int32_t)n*8+6,15,color(24,39,62));
    text(x - 3, y + 45, label, color(255,255,255));
}

static void draw_button(int32_t x, int32_t y, int32_t w, const char *label, int enabled) {
    rect(x, y, w, 23, color(enabled ? 52 : 95, enabled ? 112 : 103, enabled ? 181 : 112));
    text(x + 8, y + 7, label, color(255,255,255));
}

/* The kernel programs the PIT at 100 Hz. This clock measures uptime. */
static void decimal_string(uint64_t value, char out[21]) {
    char reverse[20]; size_t n = 0, used = 0;
    do { reverse[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) out[used++] = reverse[--n];
    out[used] = '\0';
}

static void uptime_string(uint64_t seconds, char out[9]) {
    uint64_t hours = seconds / 3600 % 100;
    uint64_t minutes = seconds / 60 % 60;
    seconds %= 60;
    out[0] = (char)('0' + hours / 10); out[1] = (char)('0' + hours % 10);
    out[2] = ':'; out[3] = (char)('0' + minutes / 10); out[4] = (char)('0' + minutes % 10);
    out[5] = ':'; out[6] = (char)('0' + seconds / 10); out[7] = (char)('0' + seconds % 10);
    out[8] = '\0';
}

static int update_clock(void) {
    rtc_datetime_t now;
    if (!rtc_read_time(&now)) return 0;
    int changed=!clock_valid || now.year!=desktop_time.year ||
        now.month!=desktop_time.month || now.day!=desktop_time.day ||
        now.hour!=desktop_time.hour || now.minute!=desktop_time.minute ||
        now.second!=desktop_time.second;
    desktop_time=now;
    clock_valid=1;
    return changed;
}

static void draw_runtime_stats(void) {
    char clock[12]="--:--:--", date[11]="---- -- --", free_mib[21];
    if(clock_valid) {
        rtc_datetime_t shown=desktop_time;
        if(clock_12h) { shown.hour%=12; if(shown.hour==0) shown.hour=12; }
        rtc_format_time(&shown,clock); rtc_format_date(&desktop_time,date);
        if(clock_12h) { clock[8]=' ';clock[9]=desktop_time.hour>=12?'P':'A';clock[10]='M';clock[11]='\0'; }
    }
    decimal_string(pmm_free_page_count() / 256, free_mib);
    int32_t x=(int32_t)fb->width-280;
    if(x<225) return;
    int32_t y=(int32_t)fb->height;
    rect(x,y-38,280,38,color(16,26,43));
    text(x+8,y-31,clock,color(255,255,255));
    text(x+8,y-17,date,color(175,197,228));
    text(x+120,y-31,"Free RAM (MiB)",color(150,180,220));
    text(x+120,y-17,free_mib,color(255,255,255));
}

static void draw_system_monitor(int32_t wx, int32_t wy, int32_t ww, int32_t wh) {
    uint64_t total = pmm_total_page_count(), free = pmm_free_page_count();
    uint64_t used = total >= free ? total - free : 0;
    char value[21], clock[9];
    rect(wx + 1, wy + 35, ww - 2, wh - 36, color(244,247,251));
    text(wx + 24, wy + 57, "MEMORY, CLOCK AND STORAGE", color(30,78,142));
    text(wx + 24, wy + 88, "Managed RAM (MiB):", color(40,52,70));
    decimal_string(total / 256, value); text(wx + 208, wy + 88, value, color(30,78,142));
    text(wx + 24, wy + 110, "Free RAM (MiB):", color(40,52,70));
    decimal_string(free / 256, value); text(wx + 208, wy + 110, value, color(30,78,142));
    text(wx + 24, wy + 132, "Used RAM (MiB):", color(40,52,70));
    decimal_string(used / 256, value); text(wx + 208, wy + 132, value, color(30,78,142));
    int32_t bar_width = ww - 48;
    rect(wx + 24, wy + 158, bar_width, 16, color(212,223,236));
    if (total) rect(wx + 24, wy + 158, (int32_t)(used * (uint64_t)bar_width / total), 16, color(44,105,198));
    text(wx + 24, wy + 192, "Uptime:", color(40,52,70));
    uptime_string(timer_ticks / 100, clock); text(wx + 112, wy + 192, clock, color(30,78,142));
    text(wx + 24, wy + 216, "Disk size (MB):", color(40,52,70));
    if(disk_capacity) { decimal_string(disk_capacity/1000000,value); text(wx+208,wy+216,value,color(30,78,142)); }
    else text(wx+208,wy+216,"Unavailable",color(178,51,51));
    if(wh>280 && clock_valid) {
        char date[11]; rtc_format_date(&desktop_time,date); rtc_format_time(&desktop_time,clock);
        text(wx+24,wy+240,"Date/time:",color(40,52,70));
        text(wx+120,wy+240,date,color(30,78,142)); text(wx+216,wy+240,clock,color(30,78,142));
    }
    if(wh>310) text(wx+24,wy+268,"File system: 15 files, 512 bytes each.",color(40,52,70));
    text(wx+24,wy+wh-20,"Esc closes. Disk space is separate from RAM.",color(40,52,70));
}

static void wrapped_text(int32_t x,int32_t y,const char *value,uint32_t columns,uint32_t rows,uint32_t c) {
    if(columns<1) return;
    if(columns>79) columns=79;
    for(uint32_t row=0;row<rows && *value;row++) {
        char part[80];uint32_t n=0;
        while(n<columns && *value) part[n++]=*value++;
        part[n]='\0';text(x,y+(int32_t)row*11,part,c);
    }
}

static void draw_settings(int32_t wx,int32_t wy,int32_t ww,int32_t wh) {
    int32_t x=wx+18;
    rect(wx+1,wy+35,ww-2,wh-36,color(244,247,251));
    text(x,wy+48,"GENERAL AND WALLPAPER",color(30,78,142));
    text(x,wy+80,"Clock format",color(40,52,70));
    draw_button(wx+160,wy+72,120,clock_12h?"12 hour":"24 hour",1);
    text(x,wy+112,"Pointer speed",color(40,52,70));
    draw_button(wx+160,wy+104,120,pointer_speed==0?"Slow":pointer_speed==1?"Normal":"Fast",1);
    text(x,wy+144,"Wallpaper",color(40,52,70));
    static const char *const names[]={"Custom","Ocean","Sunset","Forest"};
    draw_button(wx+160,wy+136,120,names[wallpaper_mode],1);
    draw_button(wx+300,wy+136,84,"Save",1);
    wrapped_text(x,wy+164,appearance_status,(uint32_t)(ww-36)/8,1,color(50,92,61));
    text(x,wy+190,"WINDOWS CONNECTION TOOLS",color(30,78,142));
    draw_button(x,wy+208,244,"Open Windows Wi-Fi",host_tools_available());
    draw_button(x,wy+240,244,"Open Windows Bluetooth",host_tools_available());
    wrapped_text(x,wy+274,host_tools_status(),(uint32_t)(ww-36)/8,2,color(50,92,61));
    if(wh>350) text(x,wy+306,"Choose networks / pair devices in Windows.",color(40,52,70));
    if(wh>365) text(x,wy+324,"Guest Wi-Fi / Bluetooth drivers: unavailable.",color(140,76,36));
    text(x,wy+wh-20,"G Wallpaper | S Save | T Clock | P Pointer",color(40,52,70));
}

static const char calculator_keys[20]={'c','(',')','\b','7','8','9','/','4','5','6','*','1','2','3','-','0','.','=','+'};
static void draw_calculator(int32_t wx,int32_t wy,int32_t ww,int32_t wh) {
    rect(wx+1,wy+35,ww-2,wh-36,color(244,247,251));
    rect(wx+18,wy+46,ww-36,56,color(222,232,245));
    uint32_t columns=(uint32_t)(ww-52)/8;
    const char *input=calculator.input;size_t count=calculator.length;
    if(count>columns)input+=count-columns;
    text(wx+26,wy+56,input,color(58,75,101));
    text(wx+26,wy+82,calculator.evaluated?calculator.result:"0",color(calculator.evaluated<0?175:30,60,110));
    int32_t bw=(ww-60)/4,bh=(wh-184)/5;
    static const char *const labels[]={"C","(",")","Back","7","8","9","/","4","5","6","*","1","2","3","-","0",".","=","+"};
    for(int i=0;i<20;i++){
        int32_t x=wx+18+(i%4)*(bw+8),y=wy+116+(i/4)*(bh+8);
        rect(x,y,bw,bh,color(i==18?32:52,i==18?136:112,i==18?94:181));
        text(x+12,y+(bh-8)/2,labels[i],color(255,255,255));
    }
    text(wx+18,wy+wh-20,"Enter = | C clear | Backspace delete",color(40,52,70));
}
static void calculator_click(int32_t mx,int32_t my,int32_t wx,int32_t wy,int32_t ww,int32_t wh){
    int32_t bw=(ww-60)/4,bh=(wh-184)/5;
    for(int i=0;i<20;i++)if(inside(mx,my,wx+18+(i%4)*(bw+8),wy+116+(i/4)*(bh+8),bw,bh)){
        calculator_key(&calculator,calculator_keys[i]);return;
    }
}

static void draw_desktop(uint8_t app, int32_t wx, int32_t wy,
                         const gui_file_t *files, uint32_t file_count,
                         int32_t selected, uint8_t editor_mode,
                         uint8_t editing_name, const char *name,
                         const char *contents, const char *status) {
    draw_wallpaper();
    text(24, 22, "PRATEEKOS", color(255,255,255));
    draw_file_icon(36, 95, "Files");
    draw_file_icon(36, 180, "Terminal");
    draw_file_icon(36, 265, "About");
    draw_file_icon(36, 350, "System");
    draw_file_icon(140, 95, "Settings");
    draw_file_icon(140, 180, "Calculator");
    rect(16,14,98,24,color(24,39,62));text(24,22,"PRATEEKOS",color(255,255,255));
    rect((int32_t)fb->width-208,14,202,24,color(24,39,62));
    text((int32_t)fb->width - 200, 22, "F6 Settings | F7 Calc", color(218,228,245));
    rect(0, (int32_t)fb->height - 38, (int32_t)fb->width, 38, color(16,26,43));
    rect(12, (int32_t)fb->height - 31, 95, 25, color(44,105,198));
    text(27, (int32_t)fb->height - 22, "PRATEEK", color(255,255,255));
    text(122, (int32_t)fb->height - 22, app == 1 ? "Files" : app == 2 ? "About" : app == 3 ? "System" : app == 4 ? "Help" : app == 5 ? "Settings" : app == 6 ? "Calculator" : "Desktop", color(218,228,245));
    draw_runtime_stats();

    if (app) {
        int32_t ww = (int32_t)fb->width - 96, wh = (int32_t)fb->height - 116;
        if (ww > 780) ww = 780;
        if (wh > 520) wh = 520;
        if (ww < 400) ww = (int32_t)fb->width - 24;
        if (wh < 260) wh = (int32_t)fb->height - 54;
        rect(wx + 5, wy + 6, ww, wh, color(7,12,22));
        rect(wx, wy, ww, wh, color(244,247,251));
        rect(wx, wy, ww, 34, color(30,47,74));
        text(wx + 14, wy + 13, app == 1 ? (editor_mode ? "Text editor" : "File Manager") : app == 3 ? "System Monitor" : app == 4 ? "Keyboard Shortcuts" : app == 5 ? "Settings" : app == 6 ? "Calculator" : "About PRATEEKOS", color(255,255,255));
        rect(wx + ww - 29, wy + 5, 22, 23, color(196,65,69));
        text(wx + ww - 23, wy + 13, "X", color(255,255,255));

        if (app == 1 && !editor_mode) {
            draw_button(wx + 12, wy + 42, 55, "New", 1);
            draw_button(wx + 74, wy + 42, 55, "Edit", selected >= 0 && (uint32_t)selected < file_count && files[selected].on_disk);
            draw_button(wx + 136, wy + 42, 67, "Delete", selected >= 0 && (uint32_t)selected < file_count && files[selected].on_disk);
            int32_t split = wx + ww / 2;
            rect(split, wy + 70, 1, wh - 70, color(198,205,216));
            text(wx + 14, wy + 75, "NAME  (RAM / DISK)", color(32,50,75));
            text(split + 14, wy + 75, "PREVIEW", color(32,50,75));
            uint32_t visible_rows = wh > 119 ? (uint32_t)(wh - 119) / 17 : 0;
            if (visible_rows > 24) visible_rows = 24;
            if (selected >= 0 && visible_rows) {
                if ((uint32_t)selected < file_scroll) file_scroll = (uint32_t)selected;
                if ((uint32_t)selected >= file_scroll + visible_rows) file_scroll = (uint32_t)selected - visible_rows + 1;
            }
            if (file_scroll >= file_count) file_scroll = 0;
            for (uint32_t row = 0; row < visible_rows && file_scroll + row < file_count; row++) {
                uint32_t i = file_scroll + row;
                int32_t row_y = wy + 94 + (int32_t)row * 17;
                if ((int32_t)i == selected) rect(wx + 8, row_y - 3, ww / 2 - 16, 16, color(203,224,250));
                text(wx + 14, row_y, files[i].on_disk ? "[D] " : "[R] ", color(69,94,128));
                text(wx + 46, row_y, files[i].name, color(37,49,66));
            }
            if (file_count == 0) text(wx + 14, wy + 103, "No files found", color(73,83,99));
            if (selected >= 0 && (uint32_t)selected < file_count) {
                uint8_t disk_data[512]; const uint8_t *ram_data = NULL;
                uint64_t ram_size = 0; size_t disk_size = 0;
                int readable = files[selected].on_disk
                    ? diskfs_read_file(files[selected].name, disk_data, sizeof(disk_data), &disk_size)
                    : ramfs_get_file(files[selected].name, &ram_data, &ram_size);
                const uint8_t *data = files[selected].on_disk ? disk_data : ram_data;
                uint64_t size = files[selected].on_disk ? disk_size : ram_size;
                int32_t tx = split + 14, ty = wy + 96;
                uint32_t columns = (uint32_t)(wx + ww - tx - 16) / 8;
                if (columns == 0) columns = 1;
                if (!readable) text(tx, ty, "Cannot read file", color(178,51,51));
                else {
                    text(tx, ty, files[selected].name, color(34,75,133)); ty += 20;
                    for (uint64_t i = 0; i < size && ty < wy + wh - 14; i++) {
                        uint8_t ch = data[i];
                        if (ch == '\r') continue;
                        if (ch == '\n') { tx = split + 14; ty += 10; continue; }
                        if (ch < 32 || ch >= 127) ch = '.';
                        char one[2] = {(char)ch, '\0'}; text(tx, ty, one, color(46,55,68)); tx += 8;
                        if ((uint32_t)(tx - split - 14) >= columns * 8) { tx = split + 14; ty += 10; }
                    }
                }
            } else text(split + 14, wy + 103, "Select a file to preview it.", color(73,83,99));
            text(wx + 14, wy + wh - 22, status, color(50,92,61));
        } else if (app == 1) {
            int32_t left = wx + 18, right = wx + ww - 18, bottom = wy + wh - 43;
            text(left, wy + 48, "File name (flat names, max 27 characters):", color(32,50,75));
            rect(left, wy + 63, ww - 36, 24, color(editing_name ? 222 : 255, editing_name ? 235 : 255, 250));
            text(left + 6, wy + 71, name, color(37,49,66));
            text(left, wy + 98, "Text (max 512 characters; Enter adds a new line):", color(32,50,75));
            rect(left, wy + 113, ww - 36, wh - 174, color(editing_name ? 255 : 222, editing_name ? 255 : 235, 250));
            int32_t tx = left + 7, ty = wy + 121;
            int32_t max_x = right - 5, max_y = bottom - 22;
            for (size_t i = 0; contents[i] && ty <= max_y; i++) {
                uint8_t ch = (uint8_t)contents[i];
                if (ch == '\n') { tx = left + 7; ty += 10; continue; }
                if (ch < 32 || ch >= 127) ch = '.';
                char one[2] = {(char)ch, '\0'}; text(tx, ty, one, color(37,49,66)); tx += 8;
                if (tx + 8 > max_x) { tx = left + 7; ty += 10; }
            }
            draw_button(wx + ww - 145, bottom, 58, "Save", 1);
            draw_button(wx + ww - 78, bottom, 60, "Cancel", 1);
            text(left, wy + wh - 19, status, color(50,92,61));
        } else if (app == 3) {
            draw_system_monitor(wx,wy,ww,wh);
        } else if (app == 6) {
            draw_calculator(wx,wy,ww,wh);
        } else if (app == 5) {
            draw_settings(wx,wy,ww,wh);
        } else if (app == 4) {
            static const char *const shortcuts[] = {
                "F1  Keyboard shortcuts", "F2  Files", "F3  System monitor", "F4  Terminal",
                "F5  Refresh file list", "F6  Settings", "F7  Calculator", "Esc Close window or cancel editing", "J/K Select next/previous file",
                "N   New disk file", "E   Edit selected disk file", "D   Delete (press again to confirm)",
                "Tab Switch filename/text field", "Ctrl+S Save file",
                "[R] Built-in file; [D] Writable disk file"
            };
            for (size_t i=0; i<sizeof(shortcuts)/sizeof(shortcuts[0]); i++) {
                int32_t y=wy+52+(int32_t)i*18;
                if(y+8>=wy+wh-10) break;
                text(wx+18,y,shortcuts[i],color(40,52,70));
            }
        } else {
            text(wx + 24, wy + 62, "PRATEEKOS", color(30,78,142));
            text(wx + 24, wy + 84, "A small x86-64 learning operating system.", color(40,52,70));
            text(wx + 24, wy + 108, "Files are stored in RAM or on the virtual disk.", color(40,52,70));
            text(wx + 24, wy + 140, "Mouse and keyboard input use PS/2 drivers.", color(40,52,70));
            text(wx + 24, wy + 170, "Click Terminal or press Enter to open the shell.", color(40,52,70));
            text(wx + 24, wy + 188, "Type desktop in the shell to return.", color(40,52,70));
        }
    }
}

static uint32_t collect_files(gui_file_t *files) {
    uint32_t count = 0;
    for (uint64_t i = 0; i < 32 && count < GUI_MAX_FILES; i++) {
        char path[GUI_NAME_SIZE];
        if (!ramfs_file_path_at(i, path, sizeof(path))) break;
        files[count].on_disk = 0;
        size_t j = 0; while (j + 1 < sizeof(files[count].name) && path[j]) { files[count].name[j] = path[j]; j++; }
        files[count++].name[j] = '\0';
    }
    char disk_names[15][28];
    uint32_t disk_count = diskfs_get_names(disk_names);
    if (disk_count > 15) disk_count = 15;
    for (uint32_t i = 0; i < disk_count && count < GUI_MAX_FILES; i++) {
        files[count].on_disk = 1;
        uint32_t j = 0; while (j < 27 && disk_names[i][j]) { files[count].name[j] = disk_names[i][j]; j++; }
        files[count++].name[j] = '\0';
    }
    return count;
}

static char key_to_char(uint8_t sc, int shift) {
    static const char plain[58] = {
        [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
        [12]='-',[13]='=',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',[23]='i',[24]='o',[25]='p',
        [26]='[',[27]=']',[30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',
        [39]=';',[40]='\'',[41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m',
        [51]=',',[52]='.',[53]='/',[57]=' '
    };
    static const char shifted[58] = {
        [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',
        [12]='_',[13]='+',[16]='Q',[17]='W',[18]='E',[19]='R',[20]='T',[21]='Y',[22]='U',[23]='I',[24]='O',[25]='P',
        [26]='{',[27]='}',[30]='A',[31]='S',[32]='D',[33]='F',[34]='G',[35]='H',[36]='J',[37]='K',[38]='L',
        [39]=':',[40]='"',[41]='~',[43]='|',[44]='Z',[45]='X',[46]='C',[47]='V',[48]='B',[49]='N',[50]='M',
        [51]='<',[52]='>',[53]='?',[57]=' '
    };
    return sc < 58 ? (shift ? shifted[sc] : plain[sc]) : '\0';
}

static size_t str_len(const char *s, size_t max) {
    size_t n = 0; while (n < max && s[n]) n++; return n;
}

static void copy_string(char *to, const char *from, size_t cap) {
    size_t i = 0; if (cap == 0) return;
    while (i + 1 < cap && from[i]) { to[i] = from[i]; i++; }
    to[i] = '\0';
}

static int valid_disk_name(const char *s) {
    size_t n = str_len(s, 28);
    if (n == 0 || n >= 28) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '/' || s[i] == '\\' || s[i] == ':') return 0;
    }
    return 1;
}

int gui_run(struct limine_framebuffer *framebuffer) {
    fb = framebuffer;
    load_appearance();calculator_init(&calculator);
    file_scroll = 0;
    cursor_visible = 0;
    clock_valid = 0;
    (void)update_clock();
    disk_capacity=ide_capacity_bytes();
    mouse_init();
    host_tools_init();
    int32_t mx = (int32_t)fb->width / 2, my = (int32_t)fb->height / 2;
    int32_t ww = (int32_t)fb->width - 96, wh = (int32_t)fb->height - 116;
    if (ww > 780) ww = 780;
    if (wh > 520) wh = 520;
    if (ww < 400) ww = (int32_t)fb->width - 24;
    if (wh < 260) wh = (int32_t)fb->height - 54;
    int32_t wx = ((int32_t)fb->width - ww) / 2, wy = ((int32_t)fb->height - wh) / 2 - 12;
    uint8_t app = 0, previous_buttons = 0, editor_mode = 0, editing_name = 0, shift = 0, control = 0;
    int32_t selected = -1, drag_x = 0, drag_y = 0;
    int32_t drawn_wx = wx, drawn_wy = wy;
    int dragging = 0, delete_armed = 0;
    gui_file_t files[GUI_MAX_FILES] = {0}; uint32_t file_count = 0;
    char editor_name[GUI_NAME_SIZE] = {0}, editor_text[GUI_TEXT_SIZE] = {0};
    char status[80] = "Select a file or create one.";
    draw_desktop(app,wx,wy,files,file_count,selected,editor_mode,editing_name,editor_name,editor_text,status);
    pointer_draw(mx,my);
    uint64_t last_second = timer_ticks / 100;
    uint32_t clock_poll=0;

    for (;;) {
        if(host_tools_poll() && app==5) {
            pointer_restore();draw_settings(wx,wy,ww,wh);pointer_draw(mx,my);
        }
        int clock_changed=0;
        /* CMOS polling keeps wall time working even if PIT interrupts stop. */
        if(++clock_poll>=8192) { clock_poll=0; clock_changed=update_clock(); }
        uint64_t second = timer_ticks / 100;
        if (second != last_second || clock_changed) {
            last_second = second;
            pointer_restore();
            draw_runtime_stats();
            if (app == 3) draw_system_monitor(wx,wy,ww,wh);
            pointer_draw(mx,my);
        }
        struct mouse_event event;
        if (mouse_poll(&event)) {
            pointer_restore();
            /* Mild acceleration makes the pointer usable at common QEMU sizes. */
            int32_t dx = event.dx, dy = event.dy;
            if(pointer_speed==0) {
                if(dx>1 || dx< -1) dx/=2;
                if(dy>1 || dy< -1) dy/=2;
            } else if(pointer_speed==2) { dx*=2;dy*=2; }
            else {
                if (dx > 8 || dx < -8) dx *= 2;
                if (dy > 8 || dy < -8) dy *= 2;
            }
            mx += dx; my -= dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if ((uint64_t)mx >= fb->width) mx = (int32_t)fb->width - 1;
            if ((uint64_t)my >= fb->height) my = (int32_t)fb->height - 1;
            int pressed = (event.buttons & 1) && !(previous_buttons & 1);
            int released = !(event.buttons & 1) && (previous_buttons & 1);
            int redraw = 0;
            if (pressed) {
                if (app) {
                    if (inside(mx,my,wx+ww-29,wy+5,22,23)) { app=0; editor_mode=0; dragging=0; redraw=1; }
                            else if (inside(mx,my,wx,wy,ww,34)) { dragging=1; drag_x=mx-wx; drag_y=my-wy; drawn_wx=wx; drawn_wy=wy; }
                    else if (app==6) { calculator_click(mx,my,wx,wy,ww,wh);redraw=1; }
                    else if (app==5) {
                        if(inside(mx,my,wx+160,wy+72,120,23)) { clock_12h=!clock_12h;redraw=1; }
                        else if(inside(mx,my,wx+160,wy+104,120,23)) { pointer_speed=(pointer_speed+1)%3;redraw=1; }
                        else if(inside(mx,my,wx+160,wy+136,120,23)) { wallpaper_mode=(wallpaper_mode+1)%4;redraw=1; }
                        else if(inside(mx,my,wx+300,wy+136,84,23)) { save_appearance();redraw=1; }
                        else if(inside(mx,my,wx+18,wy+208,244,23)) { (void)host_tools_open_wifi();redraw=1; }
                        else if(inside(mx,my,wx+18,wy+240,244,23)) { (void)host_tools_open_bluetooth();redraw=1; }
                    }
                    else if (app==1 && editor_mode) {
                        int32_t bottom=wy+wh-43;
                        if (inside(mx,my,wx+ww-145,bottom,58,23)) {
                            if (!valid_disk_name(editor_name)) copy_string(status,"Name must be 1-27 chars; no folders.",sizeof(status));
                            else if (!diskfs_write_file(editor_name,editor_text)) copy_string(status,"Save failed: check disk, format, or free slots.",sizeof(status));
                            else { copy_string(status,"Saved to disk.",sizeof(status)); editor_mode=0; file_count=collect_files(files); selected=-1; }
                            redraw=1;
                        } else if (inside(mx,my,wx+ww-78,bottom,60,23)) { editor_mode=0; copy_string(status,"Edit cancelled.",sizeof(status)); redraw=1; }
                        else if (inside(mx,my,wx+18,wy+63,ww-36,24)) { editing_name=1; redraw=1; }
                        else if (inside(mx,my,wx+18,wy+113,ww-36,wh-174)) { editing_name=0; redraw=1; }
                    } else if (app==1) {
                        int32_t by=wy+42;
                        if (inside(mx,my,wx+12,by,55,23)) {
                            editor_name[0]='\0'; editor_text[0]='\0'; editor_mode=1; editing_name=1;
                            copy_string(status,"Type a file name, press Enter, then enter text.",sizeof(status)); redraw=1;
                        } else if (inside(mx,my,wx+74,by,55,23)) {
                            if (selected>=0 && (uint32_t)selected<file_count && files[selected].on_disk) {
                                uint8_t data[512]; size_t size=0;
                                if (diskfs_read_file(files[selected].name,data,sizeof(data),&size)) {
                                    copy_string(editor_name,files[selected].name,sizeof(editor_name));
                                    size_t n=size<GUI_TEXT_SIZE-1?size:GUI_TEXT_SIZE-1;
                                    for(size_t i=0;i<n;i++) editor_text[i]=(char)data[i];
                                    editor_text[n]='\0';
                                    editor_mode=2; editing_name=0; copy_string(status,"Edit text and click Save.",sizeof(status));
                                } else copy_string(status,"Could not read disk file.",sizeof(status));
                            } else copy_string(status,"Built-in RAM files are read-only.",sizeof(status));
                            redraw=1;
                        } else if (inside(mx,my,wx+136,by,67,23)) {
                            if (selected>=0 && (uint32_t)selected<file_count && files[selected].on_disk) {
                                if (delete_armed) {
                                    if (diskfs_remove_file(files[selected].name)) { copy_string(status,"File deleted from disk.",sizeof(status)); file_count=collect_files(files); selected=-1; }
                                    else copy_string(status,"Delete failed.",sizeof(status));
                                    delete_armed=0;
                                } else { delete_armed=1; copy_string(status,"Click Delete again to confirm.",sizeof(status)); }
                            } else copy_string(status,"Select a disk file to delete.",sizeof(status));
                            redraw=1;
                        } else if (inside(mx,my,wx+8,wy+91,ww/2-16,wh-112)) {
                            int32_t row=(my-(wy+91))/17;
                            int32_t visible_rows=wh>119?(wh-119)/17:0;
                            if(visible_rows>24)visible_rows=24;
                            selected=row>=0 && row<visible_rows && file_scroll+(uint32_t)row<file_count?(int32_t)file_scroll+row:-1;
                            delete_armed=0; copy_string(status,selected>=0?"Selected file.":"No file selected.",sizeof(status)); redraw=1;
                        }
                    }
                } else if (inside(mx,my,25,85,78,70)) { app=1; editor_mode=0; selected=-1; file_count=collect_files(files); copy_string(status,"Disk files can be created, edited, and deleted.",sizeof(status)); redraw=1; }
                else if (inside(mx,my,25,170,100,75) || inside(mx,my,12,(int32_t)fb->height-31,95,25)) return 1;
                else if (inside(mx,my,25,255,78,70)) { app=2; redraw=1; }
                else if (inside(mx,my,25,340,78,70)) { app=3; redraw=1; }
                else if (inside(mx,my,129,85,92,70)) { app=5; redraw=1; }
                else if (inside(mx,my,129,170,100,75)) { app=6;calculator_init(&calculator);redraw=1; }
            }
            if (dragging && (event.buttons & 1)) {
                wx=mx-drag_x; wy=my-drag_y;
                if(wx<0)wx=0;
                if(wy<0)wy=0;
                if(wx+ww>(int32_t)fb->width)wx=(int32_t)fb->width-ww;
                if(wy+wh>(int32_t)fb->height-38)wy=(int32_t)fb->height-38-wh;
                if ((wx-drawn_wx >= 8) || (drawn_wx-wx >= 8) ||
                    (wy-drawn_wy >= 8) || (drawn_wy-wy >= 8)) {
                    redraw=1; drawn_wx=wx; drawn_wy=wy;
                }
            }
            if(released) {
                if (dragging && (wx != drawn_wx || wy != drawn_wy)) redraw=1;
                dragging=0; drawn_wx=wx; drawn_wy=wy;
            }
            previous_buttons=event.buttons;
            if(redraw) { draw_desktop(app,wx,wy,files,file_count,selected,editor_mode,editing_name,editor_name,editor_text,status); }
            pointer_draw(mx,my);
        }

        uint8_t scancode;
        if (keyboard_poll(&scancode)) {
            if (scancode == 0x2A || scancode == 0x36) { shift=1; continue; }
            if (scancode == 0xAA || scancode == 0xB6) { shift=0; continue; }
            if (scancode == 0x1D) { control=1; continue; }
            if (scancode == 0x9D) { control=0; continue; }
            if (scancode & 0x80) continue;
            if (scancode == 0x01) { if(editor_mode){editor_mode=0;copy_string(status,"Edit cancelled.",sizeof(status));}else app=0; }
            else if (!editor_mode && scancode >= 0x3B && scancode <= 0x41) {
                dragging=0; delete_armed=0;
                if (scancode==0x3B) app=4;
                else if (scancode==0x3C) { app=1; selected=-1; file_scroll=0; file_count=collect_files(files); }
                else if (scancode==0x3D) app=3;
                else if (scancode==0x3E) return 1;
                else if (scancode==0x40) app=5;
                else if (scancode==0x41) { app=6;calculator_init(&calculator); }
                else if (app==1) { file_count=collect_files(files); selected=-1; file_scroll=0; copy_string(status,"File list refreshed.",sizeof(status)); }
            } else if (app==6) {
                if(scancode==0x1C)calculator_key(&calculator,'=');
                else if(scancode==0x0E)calculator_key(&calculator,'\b');
                else if(scancode==0x37)calculator_key(&calculator,'*');
                else if(scancode==0x4A)calculator_key(&calculator,'-');
                else if(scancode==0x4E)calculator_key(&calculator,'+');
                else calculator_key(&calculator,key_to_char(scancode,shift));
            } else if (app==5) {
                if(scancode==0x11) (void)host_tools_open_wifi();
                else if(scancode==0x30) (void)host_tools_open_bluetooth();
                else if(scancode==0x14) clock_12h=!clock_12h;
                else if(scancode==0x19) pointer_speed=(pointer_speed+1)%3;
                else if(scancode==0x22) wallpaper_mode=(wallpaper_mode+1)%4;
                else if(scancode==0x1F) save_appearance();
            } else if (editor_mode && control && scancode == 0x1F) {
                if (!valid_disk_name(editor_name)) copy_string(status,"Name must be 1-27 chars; no folders.",sizeof(status));
                else if (!diskfs_write_file(editor_name,editor_text)) copy_string(status,"Save failed: check disk, format, or free slots.",sizeof(status));
                else { copy_string(status,"Saved to disk.",sizeof(status)); editor_mode=0; file_count=collect_files(files); selected=-1; }
            } else if (editor_mode) {
                if (scancode == 0x1C) {
                    if (editing_name) editing_name=0;
                    else { size_t n=str_len(editor_text,sizeof(editor_text)); if(n+1<sizeof(editor_text)){editor_text[n]='\n';editor_text[n+1]='\0';} }
                } else if (scancode == 0x0E) {
                    if (editing_name) { size_t n=str_len(editor_name,sizeof(editor_name)); if(n)editor_name[n-1]='\0'; }
                    else { size_t n=str_len(editor_text,sizeof(editor_text)); if(n)editor_text[n-1]='\0'; }
                } else if (scancode == 0x0F) editing_name=!editing_name;
                else {
                    char ch=key_to_char(scancode,shift);
                    if(ch) {
                        if(editing_name) { size_t n=str_len(editor_name,sizeof(editor_name)); if(n+1<sizeof(editor_name)) {editor_name[n]=ch;editor_name[n+1]='\0';} }
                        else { size_t n=str_len(editor_text,sizeof(editor_text)); if(n+1<sizeof(editor_text)) {editor_text[n]=ch;editor_text[n+1]='\0';} }
                    }
                }
            } else if (scancode == 0x1C && app == 0) return 1;
            else if (scancode == 0x21 && app == 0) { app=1; selected=-1; file_count=collect_files(files); }
            else if (app == 1 && !editor_mode && (scancode == 0x24 || scancode == 0x25)) {
                if (file_count != 0) {
                    if (scancode == 0x24) selected = selected < 0 ? 0 : (selected + 1 < (int32_t)file_count ? selected + 1 : 0);
                    else selected = selected <= 0 ? (int32_t)file_count - 1 : selected - 1;
                    delete_armed=0;
                    copy_string(status,"Selected with keyboard. E edit, D delete, N new.",sizeof(status));
                }
            } else if (app == 1 && !editor_mode && scancode == 0x12 && selected>=0 && (uint32_t)selected<file_count && files[selected].on_disk) {
                uint8_t data[512]; size_t size=0;
                if(diskfs_read_file(files[selected].name,data,sizeof(data),&size)) {
                    copy_string(editor_name,files[selected].name,sizeof(editor_name));
                    size_t n=size<GUI_TEXT_SIZE-1?size:GUI_TEXT_SIZE-1;
                    for(size_t i=0;i<n;i++)editor_text[i]=(char)data[i];
                    editor_text[n]='\0';
                    editor_mode=2;editing_name=0;
                }
            } else if (app == 1 && !editor_mode && scancode == 0x31) {
                editor_name[0]='\0'; editor_text[0]='\0'; editor_mode=1; editing_name=1;
                copy_string(status,"New file: type name, Enter, then text; Ctrl+S saves.",sizeof(status));
            } else if (app == 1 && !editor_mode && scancode == 0x20) {
                if (selected>=0 && (uint32_t)selected<file_count && files[selected].on_disk) {
                    if (delete_armed) {
                        if (diskfs_remove_file(files[selected].name)) { copy_string(status,"File deleted from disk.",sizeof(status)); file_count=collect_files(files); selected=-1; }
                        else copy_string(status,"Delete failed.",sizeof(status));
                        delete_armed=0;
                    } else { delete_armed=1; copy_string(status,"Press D again to confirm deletion.",sizeof(status)); }
                } else copy_string(status,"Select a [D] disk file; [R] RAM files are read-only.",sizeof(status));
            } else if (scancode == 0x1E && app == 0) app=2;
            else if (app==1 && editor_mode && scancode==0x1C && editing_name) editing_name=0;
            pointer_restore();
            draw_desktop(app,wx,wy,files,file_count,selected,editor_mode,editing_name,editor_name,editor_text,status);
            pointer_draw(mx,my);
        }
        __asm__ volatile ("pause");
    }
}
