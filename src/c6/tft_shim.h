// On the AMOLED board the faces still name TFT_eSPI and TFT_eSprite in their
// two render overloads. Both become thin subclasses of Canvas: distinct
// types, so the overloads stay distinct, but the same drawing code.
#pragma once
#include "canvas.h"

struct TFT_eSPI    : Canvas {};
struct TFT_eSprite : Canvas {};
