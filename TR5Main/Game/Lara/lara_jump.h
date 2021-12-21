#pragma once

struct ITEM_INFO;
struct COLL_INFO;

// -----------------------------
// JUMP
// Control & Collision Functions
// -----------------------------

void lara_as_jump_forward(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_jump_forward(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_freefall(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_freefall(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_reach(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_reach(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_land(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_jump_prepare(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_jump_prepare(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_backjump(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_backjump(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_rightjump(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_rightjump(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_leftjump(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_leftjump(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_upjump(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_upjump(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_fall_back(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_fall_back(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_swandive(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_swandive(ITEM_INFO* item, COLL_INFO* coll);
void lara_as_fastdive(ITEM_INFO* item, COLL_INFO* coll);
void lara_col_fastdive(ITEM_INFO* item, COLL_INFO* coll);
