#pragma once
#include "Color/Color.h"

struct ItemInfo;

namespace TEN::Effects::Items
{
	constexpr int DEFAULT_NONLETHAL_EFFECT_TIMEOUT = 48;

	void ItemBurn(ItemInfo* item, int timeout = -1);
	void ItemColorBurn(ItemInfo* item, ScriptColor const& color1 = ScriptColor(1, 1, 1), ScriptColor const& color2 = ScriptColor(1, 1, 1), int timeout = -1);
	void ItemRedLaserBurn(ItemInfo* item, int timeout);
	void ItemBlueElectricBurn(ItemInfo* item, int timeout = DEFAULT_NONLETHAL_EFFECT_TIMEOUT);
	void ItemMagicBurn(ItemInfo* item, ScriptColor const& color1 = ScriptColor(1, 1, 1), ScriptColor const& color2 = ScriptColor(1, 1, 1), int timeout = -1);
	void ItemElectricBurn(ItemInfo* item, int timeout = DEFAULT_NONLETHAL_EFFECT_TIMEOUT);
	void ItemSmoke(ItemInfo* item, int timeout = DEFAULT_NONLETHAL_EFFECT_TIMEOUT);
	void LavaBurn(ItemInfo* item);
	void LaraBreath(ItemInfo* item);
}
