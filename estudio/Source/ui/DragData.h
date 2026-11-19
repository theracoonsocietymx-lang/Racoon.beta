#pragma once
#include "../engine/PluginCatalog.h"

namespace racoon::ui
{
// What the browser drags onto the timeline or the mixer.
namespace DragData
{
inline juce::var forItem (const CatalogItem& it)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", "plugin");
    o->setProperty ("source", (int) it.source);
    o->setProperty ("name", it.name);
    o->setProperty ("builtIn", it.builtInType);
    o->setProperty ("instrument", it.isInstrument);
    o->setProperty ("available", it.available);
    if (auto xml = it.desc.createXml()) o->setProperty ("desc", xml->toString());
    return juce::var (o);
}

inline juce::var forFile (const juce::File& f)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", "file");
    o->setProperty ("path", f.getFullPathName());
    return juce::var (o);
}

inline bool isPlugin (const juce::var& v) { return v.getProperty ("kind", {}).toString() == "plugin"; }
inline bool isFile (const juce::var& v)   { return v.getProperty ("kind", {}).toString() == "file"; }
inline juce::File file (const juce::var& v) { return juce::File (v.getProperty ("path", {}).toString()); }

inline CatalogItem item (const juce::var& v)
{
    CatalogItem it;
    it.source = (CatalogItem::Source) (int) v.getProperty ("source", 0);
    it.name = v.getProperty ("name", {}).toString();
    it.builtInType = v.getProperty ("builtIn", {}).toString();
    it.isInstrument = v.getProperty ("instrument", false);
    it.available = v.getProperty ("available", true);
    if (auto xml = juce::parseXML (v.getProperty ("desc", {}).toString()))
        it.desc.loadFromXml (*xml);
    return it;
}

inline bool isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;caf");
}

inline bool isMidiFile (const juce::File& f)
{
    return f.hasFileExtension ("mid;midi");
}
}
}
