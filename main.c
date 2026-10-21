/* Angry Birds CE - expanded, calculator-friendly arcade game.
 * Controls: arrows drag the bird backwards; 2nd launches; Enter restarts;
 * Clear exits. The launch vector is computed from the pulled bird position
 * to the fixed slingshot anchor, so the shot always follows the sling line.
 */
#include <graphx.h>
#include <keypadc.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include "game_sprites.h"

#define SW 320
#define SH 240
#define GROUND 218
#define ANCHOR_X 61.0f
#define ANCHOR_Y 158.0f
#define BIRD_R 7
#define MAX_BIRDS 4
#define MAX_BLOCKS 30
#define MAX_PIGS 8
#define GRAVITY 0.24f
#define BLOCK_GRAVITY 0.18f
#define PI 3.14159265f

enum { C_SKY=1, C_CLOUD, C_GRASS, C_GRASS_EDGE, C_WOOD, C_WOOD_EDGE,
       C_PIG, C_PIG_DARK, C_RED, C_RED_DARK, C_BEAK, C_WHITE, C_BLACK,
       C_STONE, C_TEXT, C_HIGHLIGHT, C_GOLD };
typedef struct { float x,y,vx,vy; uint8_t active,spent; } Bird;
typedef struct { float x,y,vx,vy; int w,h; uint8_t alive,hp,moving; } Block;
typedef struct { float x,y; uint8_t alive; } Pig;
typedef enum { MENU, PLAYING, WON, LOST, CHEATS, MODS } GameState;

static Bird bird;
static Block blocks[MAX_BLOCKS];
static Pig pigs[MAX_PIGS];
static int block_count,pig_count,birds_left,score,level=1;
static float pull_x=40.0f,pull_y=174.0f;
static GameState state=MENU;
static uint8_t was_fire=0,was_reset=0,was_up=0,was_down=0;
static int settle_frames=0;
static int menu_choice=0,cheat_choice=0;
static uint8_t cheat_infinite_birds=0,cheat_super_launch=0,cheat_easy_towers=0;

static void set_rgb(int i,int r,int g,int b){ gfx_palette[i]=gfx_RGBTo1555(r,g,b); }
static void setup_palette(void){
    gfx_SetDefaultPalette(gfx_8bpp);
    set_rgb(C_SKY,112,190,238); set_rgb(C_CLOUD,245,250,255);
    set_rgb(C_GRASS,100,190,65); set_rgb(C_GRASS_EDGE,65,145,44);
    set_rgb(C_WOOD,190,126,64); set_rgb(C_WOOD_EDGE,120,75,37);
    set_rgb(C_PIG,120,205,72); set_rgb(C_PIG_DARK,55,125,40);
    set_rgb(C_RED,220,48,45); set_rgb(C_RED_DARK,145,24,30);
    set_rgb(C_BEAK,255,180,35); set_rgb(C_WHITE,255,255,255);
    set_rgb(C_BLACK,28,28,34); set_rgb(C_STONE,135,145,155);
    set_rgb(C_TEXT,24,37,58); set_rgb(C_HIGHLIGHT,255,239,125);
    set_rgb(C_GOLD,255,190,35);
}
static float minf(float a,float b){return a<b?a:b;}
static float maxf(float a,float b){return a>b?a:b;}
static bool overlap(float x,float y,float w,float h,float bx,float by,float bw,float bh){
    return x<bx+bw && x+w>bx && y<by+bh && y+h>by;
}
static void add_block(int x,int y,int w,int h,int hp){
    if(block_count>=MAX_BLOCKS)return;
    blocks[block_count++]=(Block){(float)x,(float)y,0,0,w,h,1,(uint8_t)hp,0};
}
static void add_pig(int x,int y){if(pig_count<MAX_PIGS)pigs[pig_count++]=(Pig){(float)x,(float)y,1};}
static void reset_bird(void){bird.x=ANCHOR_X-BIRD_R;bird.y=ANCHOR_Y-BIRD_R;bird.vx=bird.vy=0;bird.active=bird.spent=0;}

/* Towers vary by level. More tiers, pigs, and narrower supports are introduced
 * as the level rises; dimensions stay inside the 320x240 screen. */
static void build_level(void){
    int tiers=cheat_easy_towers ? 2 : 3+((level-1)/2); if(tiers>5)tiers=5;
    int base=GROUND-40; int left=188-(level%3)*5;
    int vertical_hp=(!cheat_easy_towers && level>=5)?2:1;
    block_count=0;pig_count=0;birds_left=MAX_BIRDS;settle_frames=0;
    pull_x=40;pull_y=174;reset_bird();state=PLAYING;
    /* broad, sturdy base */
    add_block(left,base,10,40,2); add_block(left+68,base,10,40,2);
    add_block(left,base-10,78,10,2);
    /* stacked rooms with different openings and support patterns */
    for(int t=1;t<=tiers;t++){
        int y=base-10-t*27;
        if((level+t)%3==0){
            add_block(left+8,y,10,27,vertical_hp); add_block(left+60,y,10,27,vertical_hp);
            add_block(left+8,y-9,62,9,2);
            add_pig(left+30,y-18);
        }else if((level+t)%3==1){
            add_block(left+15,y,9,27,vertical_hp); add_block(left+49,y,9,27,vertical_hp);
            add_block(left+15,y-9,43,9,2);
            if(t%2==0)add_pig(left+30,y-18);
        }else{
            add_block(left+4,y,9,27,vertical_hp); add_block(left+55,y,9,27,vertical_hp);
            add_block(left+4,y-9,60,9,2);
            add_pig(left+25,y-18);
        }
    }
    /* Always put a visible target in the lower chamber and another near top. */
    add_pig(left+29,base-27);
    if(level>=2)add_pig(left+31,base-10-tiers*27-18);
    if(level>=4 && tiers>=3)add_pig(left+16,base-10-2*27-18);
    if(level>=7)add_pig(left+43,base-10-((level%tiers)+1)*27-18);
    if(level>=10)add_pig(left+18,base-27);
    /* Keep target count bounded and make later levels progressively tougher. */
}
static void draw_bird(int x,int y){
    gfx_SetColor(C_RED_DARK);gfx_FillCircle(x+7,y+8,7);
    gfx_SetColor(C_RED);gfx_FillCircle(x+6,y+6,7);
    gfx_SetColor(C_RED_DARK);gfx_FillCircle(x+3,y+1,3);
    gfx_SetColor(C_WHITE);gfx_FillCircle(x+8,y+4,2);
    gfx_SetColor(C_BLACK);gfx_FillCircle(x+9,y+4,1);
    gfx_SetColor(C_BEAK);gfx_FillTriangle(x+10,y+7,x+18,y+8,x+10,y+10);
}
static void draw_pig(const Pig *p){
    if(!p->alive)return;int x=(int)p->x,y=(int)p->y;
    gfx_SetColor(C_PIG_DARK);gfx_FillCircle(x+9,y+10,9);
    gfx_SetColor(C_PIG);gfx_FillCircle(x+8,y+8,8);gfx_FillCircle(x+2,y+1,3);gfx_FillCircle(x+14,y+1,3);
    gfx_SetColor(C_PIG_DARK);gfx_FillCircle(x+5,y+9,2);gfx_FillCircle(x+11,y+9,2);
    gfx_SetColor(C_PIG);gfx_FillCircle(x+5,y+8,1);gfx_FillCircle(x+11,y+8,1);
    gfx_SetColor(C_WHITE);gfx_FillCircle(x+5,y+4,2);gfx_FillCircle(x+11,y+4,2);
    gfx_SetColor(C_BLACK);gfx_FillCircle(x+6,y+4,1);gfx_FillCircle(x+12,y+4,1);
}
static void draw_block(const Block *b){
    if(!b->alive)return;
    gfx_SetColor(b->hp>1?C_STONE:C_WOOD);gfx_FillRectangle((int)b->x,(int)b->y,b->w,b->h);
    gfx_SetColor(C_WOOD_EDGE);gfx_Rectangle((int)b->x,(int)b->y,b->w,b->h);
    if(b->w>b->h)gfx_Line((int)b->x+3,(int)b->y+2,(int)b->x+b->w-4,(int)b->y+2);
    else gfx_Line((int)b->x+2,(int)b->y+4,(int)b->x+2,(int)b->y+b->h-4);
}
static void text_center(const char *s,int y){gfx_PrintStringXY(s,(SW-(int)strlen(s)*6)/2,y);}
static void draw_menu(void){
    gfx_SetColor(C_SKY);gfx_FillScreen(C_SKY);
    gfx_SetColor(C_CLOUD);gfx_FillCircle(42,48,13);gfx_FillCircle(57,44,16);gfx_FillCircle(73,49,11);
    gfx_SetColor(C_GRASS);gfx_FillRectangle(0,GROUND,SW,SH-GROUND);
    gfx_SetColor(C_GRASS_EDGE);gfx_HorizLine(0,GROUND,SW);
    gfx_SetTextFGColor(C_TEXT);text_center("ANGRY BIRDS CE",35);
    gfx_SetColor(C_GOLD);gfx_FillCircle(160,76,22);draw_bird(151,67);
    const char *items[3]={"PLAY","CHEATS","MODS"};
    for(int i=0;i<3;i++){
        int y=117+i*25;
        if(i==menu_choice){gfx_SetColor(C_HIGHLIGHT);gfx_FillRectangle(96,y-3,128,20);}
        gfx_SetTextFGColor(C_TEXT);text_center(items[i],y);
    }
    gfx_SetTextFGColor(C_TEXT);text_center("UP/DOWN SELECT   2ND: OPEN",202);
    gfx_PrintStringXY("CLEAR: EXIT",8,226);
}
static void draw_cheats(void){
    gfx_SetColor(C_SKY);gfx_FillScreen(C_SKY);gfx_SetTextFGColor(C_TEXT);
    text_center("CHEATS",32);
    const char *names[3]={"INFINITE BIRDS","SUPER LAUNCH","EASY TOWERS"};
    uint8_t vals[3]={cheat_infinite_birds,cheat_super_launch,cheat_easy_towers};
    for(int i=0;i<3;i++){
        int y=78+i*30;
        if(i==cheat_choice){gfx_SetColor(C_HIGHLIGHT);gfx_FillRectangle(42,y-4,236,22);}
        gfx_SetTextFGColor(C_TEXT);gfx_PrintStringXY(names[i],50,y);
        gfx_PrintStringXY(vals[i]?"ON":"OFF",238,y);
    }
    gfx_SetTextFGColor(C_TEXT);text_center("2ND: TOGGLE   CLEAR: BACK",190);
    text_center("CHEATS APPLY TO GAMEPLAY",208);
}
static void draw_mods(void){
    gfx_SetColor(C_SKY);gfx_FillScreen(C_SKY);gfx_SetTextFGColor(C_TEXT);
    text_center("MODS",50);text_center("MOD SUPPORT COMING LATER",96);
    text_center("THIS BUTTON IS A PLACEHOLDER",120);
    text_center("CLEAR OR ENTER: BACK",188);
}
static void draw_scene(void){
    gfx_SetColor(C_SKY);gfx_FillScreen(C_SKY);
    gfx_SetColor(C_CLOUD);gfx_FillCircle(40,49,12);gfx_FillCircle(53,46,15);gfx_FillCircle(68,50,10);
    gfx_FillCircle(143,73,8);gfx_FillCircle(154,69,12);gfx_FillCircle(166,73,8);
    gfx_SetColor(C_GRASS);gfx_FillRectangle(0,GROUND,SW,SH-GROUND);
    gfx_SetColor(C_GRASS_EDGE);gfx_HorizLine(0,GROUND,SW);
    /* Draw fixed slingshot anchor and elastic stretched to the pulled bird. */
    gfx_SetColor(C_WOOD_EDGE);gfx_Line(52,GROUND,(int)ANCHOR_X,(int)ANCHOR_Y);gfx_Line(71,GROUND,(int)ANCHOR_X,(int)ANCHOR_Y);
    if(!bird.active && birds_left>0 && state==PLAYING){
        gfx_SetColor(C_RED_DARK);gfx_Line(54,(int)ANCHOR_Y-1,(int)pull_x,(int)pull_y);
        gfx_Line(69,(int)ANCHOR_Y-1,(int)pull_x,(int)pull_y);
        gfx_SetColor(C_HIGHLIGHT);gfx_Circle((int)pull_x,(int)pull_y,2);
        /* Dotted guide shows the actual launch direction from pull point to anchor. */
        gfx_SetColor(C_WHITE);for(int n=1;n<=5;n++)gfx_FillCircle((int)(pull_x+(ANCHOR_X-pull_x)*n/6),(int)(pull_y+(ANCHOR_Y-pull_y)*n/6),1);
    }
    for(int i=0;i<block_count;i++)draw_block(&blocks[i]);
    for(int i=0;i<pig_count;i++)draw_pig(&pigs[i]);
    if(bird.active)draw_bird((int)bird.x,(int)bird.y);
    else if(birds_left>0 && state==PLAYING)draw_bird((int)pull_x-6,(int)pull_y-6);
    gfx_SetTextFGColor(C_TEXT);gfx_PrintStringXY("ANGRY BIRDS CE",8,5);
    gfx_PrintStringXY("ARROWS AIM  2ND FIRE  ENTER RESET",8,18);
    gfx_PrintStringXY("LV",8,32);gfx_SetTextXY(24,32);gfx_PrintUInt((unsigned)level,2);
    gfx_PrintStringXY("BIRDS",55,32);gfx_SetTextXY(91,32);gfx_PrintUInt((unsigned)birds_left,1);
    gfx_PrintStringXY("SCORE",112,32);gfx_SetTextXY(158,32);gfx_PrintUInt((unsigned)score,5);
    if(state==WON){gfx_SetColor(C_HIGHLIGHT);gfx_FillRectangle(52,91,216,49);gfx_SetTextFGColor(C_TEXT);text_center("LEVEL CLEARED!",100);text_center("2ND: NEXT LEVEL",118);}
    if(state==LOST){gfx_SetColor(C_HIGHLIGHT);gfx_FillRectangle(52,91,216,49);gfx_SetTextFGColor(C_TEXT);text_center("OUT OF BIRDS!",100);text_center("ENTER: RETRY LEVEL",118);}
}
static void damage_blocks(void){
    for(int i=0;i<block_count;i++){
        Block *b=&blocks[i];if(!b->alive)continue;
        if(overlap(bird.x,bird.y,14,14,b->x,b->y,b->w,b->h)){
            float impact=fabsf(bird.vx)+fabsf(bird.vy)*0.65f;
            b->hp=(impact>11.0f)?0:(b->hp>0?b->hp-1:0);
            if(b->hp==0){b->alive=0;score+=100;}
            else {b->moving=1;b->vx=bird.vx*0.18f;b->vy=bird.vy*0.12f;}
            /* Transfer some momentum to adjacent pieces so the tower can topple. */
            for(int j=0;j<block_count;j++)if(j!=i && blocks[j].alive){
                float cx=blocks[j].x+blocks[j].w*0.5f,cy=blocks[j].y+blocks[j].h*0.5f;
                float bx=b->x+b->w*0.5f,by=b->y+b->h*0.5f;
                if(fabsf(cx-bx)<32 && fabsf(cy-by)<34){blocks[j].moving=1;blocks[j].vx+=bird.vx*0.035f;blocks[j].vy-=0.25f;}
            }
            if(bird.x+7<b->x+b->w*0.5f)bird.vx=-fabsf(bird.vx)*0.30f;else bird.vx=fabsf(bird.vx)*0.30f;
            bird.vy=-fabsf(bird.vy)*0.18f;bird.x+=bird.vx;bird.y+=bird.vy;break;
        }
    }
}
static void update_blocks(void){
    for(int i=0;i<block_count;i++){
        Block *b=&blocks[i];if(!b->alive||!b->moving)continue;
        b->vy+=BLOCK_GRAVITY;b->x+=b->vx;b->y+=b->vy;b->vx*=0.985f;
        if(b->y+b->h>=GROUND){b->y=GROUND-b->h;b->vy=0;b->vx*=0.75f;if(fabsf(b->vx)<0.15f)b->moving=0;}
        if(b->x<0){b->x=0;b->vx=fabsf(b->vx)*0.35f;}if(b->x+b->w>SW){b->x=SW-b->w;b->vx=-fabsf(b->vx)*0.35f;}
        /* Landing on another block provides simple, stable stack physics. */
        for(int j=0;j<block_count;j++)if(j!=i&&blocks[j].alive){Block *o=&blocks[j];
            if(b->vy>=0 && b->y+b->h>=o->y && b->y+b->h-b->vy<=o->y && b->x+b->w>o->x+2 && b->x<o->x+o->w-2){
                b->y=o->y-b->h;b->vy=0;b->vx*=0.78f;if(fabsf(b->vx)<0.18f)b->moving=0;
            }
        }
    }
}
static void update_game(void){
    update_blocks();
    if(bird.active){
        bird.vy+=GRAVITY;bird.x+=bird.vx;bird.y+=bird.vy;
        for(int i=0;i<pig_count;i++){Pig *p=&pigs[i];if(p->alive&&overlap(bird.x,bird.y,14,14,p->x,p->y,17,17)){p->alive=0;score+=500;bird.vx*=0.58f;bird.vy*=0.58f;}}
        damage_blocks();
        /* Falling debris can knock out pigs too. */
        for(int i=0;i<pig_count;i++)if(pigs[i].alive){Pig *p=&pigs[i];for(int j=0;j<block_count;j++){Block *b=&blocks[j];if(b->alive&&b->moving&&overlap(p->x,p->y,17,17,b->x,b->y,b->w,b->h)){p->alive=0;score+=350;break;}}}
        if(bird.y+14>=GROUND){bird.y=GROUND-14;bird.active=0;}
        if(bird.x>SW+12||bird.x<-25||bird.y>SH+12||(fabsf(bird.vx)<0.18f&&fabsf(bird.vy)<0.28f))bird.active=0;
        if(!bird.active&&!bird.spent){bird.spent=1;if(!cheat_infinite_birds&&birds_left>0)birds_left--;else if(cheat_infinite_birds)birds_left=MAX_BIRDS;}
    }
    int living=0,moving=0;for(int i=0;i<pig_count;i++)if(pigs[i].alive)living++;
    for(int i=0;i<block_count;i++)if(blocks[i].alive&&blocks[i].moving)moving++;
    if(moving==0)settle_frames++;else settle_frames=0;
    if(living==0)state=WON;
    else if(birds_left==0&&!bird.active&&settle_frames>15)state=LOST;
}
static void launch(void){
    if(state!=PLAYING||bird.active||birds_left<=0)return;
    /* Pull-back vector is reversed: launch direction = fixed anchor - pulled point. */
    float dx=ANCHOR_X-pull_x,dy=ANCHOR_Y-pull_y;
    float length=sqrtf(dx*dx+dy*dy);
    if(length<5.0f)return;
    /* Clamp the pull circle and convert pull distance into stronger launch speed. */
    if(length>66.0f){dx*=66.0f/length;dy*=66.0f/length;length=66.0f;}
    float speed=length*(cheat_super_launch?0.72f:0.43f)+(cheat_super_launch?5.0f:2.5f);
    bird.x=ANCHOR_X-BIRD_R;bird.y=ANCHOR_Y-BIRD_R;
    bird.vx=(dx/length)*speed;bird.vy=(dy/length)*speed;
    bird.active=1;bird.spent=0;
}
int main(void){
    gfx_Begin();gfx_SetDrawBuffer();setup_palette();kb_SetMode(MODE_3_CONTINUOUS);
    build_level();state=MENU;
    while(1){
        kb_Scan();if(kb_Data[6]&kb_Clear)break;
        uint8_t fire=(kb_Data[1]&kb_2nd)!=0;
        uint8_t reset=(kb_Data[6]&kb_Enter)!=0;
        uint8_t up=(kb_Data[7]&kb_Up)!=0,down=(kb_Data[7]&kb_Down)!=0;
        if(state==MENU){
            if(up&&!was_up){menu_choice=(menu_choice+2)%3;}
            if(down&&!was_down){menu_choice=(menu_choice+1)%3;}
            if((fire&&!was_fire)||(reset&&!was_reset)){
                if(menu_choice==0){score=0;level=1;build_level();state=PLAYING;}
                else if(menu_choice==1){cheat_choice=0;state=CHEATS;}
                else state=MODS;
            }
        }else if(state==CHEATS){
            if(up&&!was_up)cheat_choice=(cheat_choice+2)%3;
            if(down&&!was_down)cheat_choice=(cheat_choice+1)%3;
            if(fire&&!was_fire){
                if(cheat_choice==0)cheat_infinite_birds=!cheat_infinite_birds;
                else if(cheat_choice==1)cheat_super_launch=!cheat_super_launch;
                else cheat_easy_towers=!cheat_easy_towers;
            }
            if(reset&&!was_reset){state=MENU;}
        }else if(state==MODS){if(reset&&!was_reset)state=MENU;}
        else{
            if(fire&&!was_fire){if(state==WON){level++;build_level();}else if(state==PLAYING)launch();}
            if(reset&&!was_reset){if(state==LOST){build_level();}else if(state==WON){level=1;score=0;build_level();}else if(state==PLAYING){build_level();}}
            if(state==PLAYING&&!bird.active){
                if(kb_Data[7]&kb_Left)pull_x-=1.15f;
                if(kb_Data[7]&kb_Right)pull_x+=1.15f;
                if(kb_Data[7]&kb_Up)pull_y-=1.15f;
                if(kb_Data[7]&kb_Down)pull_y+=1.15f;
                /* Keep the pulled point on a radius-66 circle around the anchor. */
                float dx=pull_x-ANCHOR_X,dy=pull_y-ANCHOR_Y,d=sqrtf(dx*dx+dy*dy);
                if(d>66.0f){pull_x=ANCHOR_X+dx*66.0f/d;pull_y=ANCHOR_Y+dy*66.0f/d;}
                if(pull_x<8)pull_x=8;if(pull_x>ANCHOR_X+62)pull_x=ANCHOR_X+62;
                if(pull_y<72)pull_y=72;if(pull_y>GROUND-8)pull_y=GROUND-8;
            }
            update_game();
        }
        was_fire=fire;was_reset=reset;was_up=up;was_down=down;
        if(state==MENU)draw_menu();else if(state==CHEATS)draw_cheats();else if(state==MODS)draw_mods();else draw_scene();gfx_SwapDraw();
    }
    gfx_End();return 0;
}
