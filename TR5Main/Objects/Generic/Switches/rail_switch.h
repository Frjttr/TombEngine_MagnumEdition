#pragma once

struct ITEM_INFO;
struct COLL_INFO;

namespace TEN::Entities::Switches
{
	void RailSwitchCollision(short itemNum, ITEM_INFO* l, COLL_INFO* coll);
}