#pragma once
#include "../engine/Session.h"

namespace racoon::ui
{
// Popup menus shared by the timeline, the mixer and the browser.
namespace Menus
{
using ItemCallback = std::function<void (const CatalogItem&)>;
juce::PopupMenu instrumentMenu (Session&, ItemCallback);
juce::PopupMenu effectMenu (Session&, ItemCallback);
}
}
