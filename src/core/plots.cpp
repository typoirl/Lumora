// SPDX-License-Identifier: GPL-3.0-or-later
#include "plots.h"

#include <QJsonArray>
#include <QObject>

#include <algorithm>

namespace lumora {

namespace {
const char *kindName(PlotItem::Kind k)
{
    switch (k) {
    case PlotItem::Fixture: return "fixture";
    case PlotItem::Object: return "object";
    case PlotItem::Text: return "text";
    case PlotItem::Image: return "image";
    }
    return "fixture";
}
} // namespace

QJsonObject PlotItem::toJson() const
{
    QJsonObject o{{"kind", kindName(kind)}, {"x", rect.x()}, {"y", rect.y()}, {"w", rect.width()}, {"h", rect.height()}};
    switch (kind) {
    case Fixture:
        o["fixture"] = fid;
        if (round)
            o["round"] = true;
        break;
    case Object:
        o["object"] = object;
        break;
    case Text:
        o["fontSize"] = fontSize;
        break;
    case Image:
        if (imageId > 0) {
            o["imageId"] = imageId;
        } else {
            o["image"] = QString::fromLatin1(image.toBase64());
            o["imageName"] = imageName;
        }
        if (litImageId > 0)
            o["litImage"] = litImageId;
        break;
    }
    if (group)
        o["group"] = group;
    if (!link.isEmpty())
        o["link"] = link;
    if (!text.isEmpty())
        o["text"] = text;
    if (!color.isEmpty())
        o["color"] = color;
    return o;
}

PlotItem PlotItem::fromJson(const QJsonObject &o)
{
    PlotItem it;
    const QString k = o["kind"].toString();
    it.kind = k == QLatin1String("object") ? Object : k == QLatin1String("text") ? Text : k == QLatin1String("image") ? Image : Fixture;
    it.rect = QRectF(o["x"].toDouble(), o["y"].toDouble(), std::max(4.0, o["w"].toDouble(48)), std::max(4.0, o["h"].toDouble(48)));
    it.fid = o["fixture"].toInt();
    it.round = o["round"].toBool(false);
    it.object = o["object"].toString();
    it.text = o["text"].toString();
    it.color = o["color"].toString();
    it.fontSize = std::clamp(o["fontSize"].toDouble(14.0), 4.0, 400.0);
    it.imageId = std::max(0, o["imageId"].toInt(0));
    it.litImageId = std::max(0, o["litImage"].toInt(0));
    it.image = QByteArray::fromBase64(o["image"].toString().toLatin1());
    it.imageName = o["imageName"].toString();
    it.group = std::max(0, o["group"].toInt(0));
    it.link = o["link"].toString();
    return it;
}

void Plot::setBackgroundColor(const QString &color)
{
    clearBackground();
    backgroundColor = color;
}

void Plot::setBackgroundImage(int imageId, const QRectF &rect)
{
    clearBackground();
    backgroundImageId = imageId;
    backgroundRect = rect;
}

void Plot::setBackgroundImage(const QByteArray &bytes, const QString &name, const QRectF &rect)
{
    clearBackground();
    backgroundImage = bytes;
    backgroundName = name;
    backgroundRect = rect;
}

void Plot::clearBackground()
{
    backgroundColor.clear();
    backgroundImageId = 0;
    backgroundImage.clear();
    backgroundName.clear();
    backgroundRect = QRectF();
}

QString Plot::displayName() const
{
    return name.isEmpty() ? QObject::tr("Plot %1").arg(id) : name;
}

QRectF Plot::bounds() const
{
    QRectF r;
    for (const PlotItem &it : items)
        r = r.isNull() ? it.rect : r.united(it.rect);
    return r;
}

int Plot::indexOfFixture(int fid) const
{
    for (int i = 0; i < items.size(); ++i)
        if (items[i].kind == PlotItem::Fixture && items[i].fid == fid)
            return i;
    return -1;
}

int Plot::nextGroup() const
{
    int g = 1;
    for (const PlotItem &it : items)
        g = std::max(g, it.group + 1);
    return g;
}

QVector<int> Plot::groupOf(int index) const
{
    if (index < 0 || index >= items.size())
        return {};
    if (!items[index].group)
        return {index};
    QVector<int> out;
    for (int i = 0; i < items.size(); ++i)
        if (items[i].group == items[index].group)
            out << i;
    return out;
}

QJsonObject Plot::toJson() const
{
    QJsonArray a;
    for (const PlotItem &it : items)
        a.append(it.toJson());
    QJsonObject o{{"id", id}, {"name", name}, {"items", a}, {"zoom", zoom}, {"panX", panX}, {"panY", panY}};
    if (zoomLocked)
        o["zoomLocked"] = true;
    if (!backgroundColor.isEmpty())
        o["backgroundColor"] = backgroundColor;
    if (hasBackgroundImage()) {
        QJsonObject b{{"x", backgroundRect.x()}, {"y", backgroundRect.y()}, {"w", backgroundRect.width()}, {"h", backgroundRect.height()}};
        if (backgroundImageId > 0) {
            b["imageId"] = backgroundImageId;
        } else {
            b["image"] = QString::fromLatin1(backgroundImage.toBase64());
            b["name"] = backgroundName;
        }
        o["backgroundImage"] = b;
    }
    return o;
}

Plot Plot::fromJson(const QJsonObject &o)
{
    Plot p;
    p.id = o["id"].toInt();
    p.name = o["name"].toString();
    for (const auto &v : o["items"].toArray()) {
        const QJsonObject io = v.toObject();
        // Shows before a.7.2.2 kept the background as a picture marked "background": the first
        // one becomes the plot's background.
        if (io["background"].toBool(false)) {
            const PlotItem it = PlotItem::fromJson(io);
            if (p.backgroundImage.isEmpty() && !it.image.isEmpty())
                p.setBackgroundImage(it.image, it.imageName, it.rect);
            continue;
        }
        p.items << PlotItem::fromJson(io);
    }
    p.backgroundColor = o["backgroundColor"].toString(p.backgroundColor);
    if (o.contains("backgroundImage")) {
        const QJsonObject b = o["backgroundImage"].toObject();
        const QRectF r(b["x"].toDouble(), b["y"].toDouble(), std::max(4.0, b["w"].toDouble(800)), std::max(4.0, b["h"].toDouble(600)));
        if (b["imageId"].toInt(0) > 0)
            p.setBackgroundImage(b["imageId"].toInt(), r);
        else
            p.setBackgroundImage(QByteArray::fromBase64(b["image"].toString().toLatin1()), b["name"].toString(), r);
    }
    p.zoom = std::clamp(o["zoom"].toDouble(1.0), 0.1, 8.0);
    p.panX = o["panX"].toDouble(0);
    p.panY = o["panY"].toDouble(0);
    p.zoomLocked = o["zoomLocked"].toBool(false);
    return p;
}

} // namespace lumora
