// SPDX-License-Identifier: GPL-3.0-or-later
#include "media.h"

#include "show.h"

#include <QHash>
#include <QSet>
#include <QPainter>
#include <QPainterPath>

namespace lumora {

namespace {
const QStringList kWheelAttributes = {"Gobo1", "Gobo2", "AnimationWheel"};
} // namespace

QImage mediaImage(const FixtureType *type, const QString &key)
{
    if (!type || key.isEmpty())
        return {};
    static QHash<QString, QImage> cache;
    const QString cacheKey = type->id + '\n' + key;
    auto it = cache.constFind(cacheKey);
    if (it != cache.constEnd())
        return it.value();
    QImage img;
    img.loadFromData(type->media.value(key));
    cache.insert(cacheKey, img);
    return img;
}

QImage slotImage(Show *show, int fid, const QString &attribute, double value)
{
    const FixtureMode *m = show->fixtureMode(fid);
    const ChannelDef *c = m ? m->channel(attribute) : nullptr;
    if (!c)
        return {};
    const ChannelSet *s = c->setFor(value);
    return s && !s->media.isEmpty() ? mediaImage(show->fixtureType(fid), s->media) : QImage();
}

QImage presetImage(Show *show, const Preset &preset)
{
    auto fromType = [&](const QString &typeId, const RawAttr &values) -> QImage {
        const FixtureType *t = show->library().find(typeId);
        if (!t || t->media.isEmpty())
            return {};
        for (const QString &attr : kWheelAttributes) {
            if (!values.contains(attr))
                continue;
            const double v = values.value(attr);
            for (const FixtureMode &m : t->modes)
                if (const ChannelDef *c = m.channel(attr))
                    if (const ChannelSet *s = c->setFor(v); s && !s->media.isEmpty())
                        return mediaImage(t, s->media);
        }
        return {};
    };
    for (auto it = preset.perFixture.begin(); it != preset.perFixture.end(); ++it)
        if (const Fixture *f = show->fixture(it.key())) {
            QImage img = fromType(f->typeId, it.value());
            if (!img.isNull())
                return img;
        }
    if (!preset.universal.isEmpty()) {
        QSet<QString> seen;
        for (const Fixture &f : show->fixtures())
            if (!seen.contains(f.typeId)) {
                seen.insert(f.typeId);
                QImage img = fromType(f.typeId, preset.universal);
                if (!img.isNull())
                    return img;
            }
    }
    for (auto it = preset.perType.begin(); it != preset.perType.end(); ++it) {
        QImage img = fromType(it.key(), it.value());
        if (!img.isNull())
            return img;
    }
    return {};
}

QPixmap goboPixmap(const QImage &image, int size)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QString::number(image.cacheKey()) + '/' + QString::number(size);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(QRectF(0.5, 0.5, size - 1, size - 1));
    p.setClipPath(clip);
    p.fillRect(pm.rect(), QColor(8, 9, 11));
    // Gobo images are white where light passes: draw them as warm light.
    QImage tinted = image.convertToFormat(QImage::Format_ARGB32).scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    for (int y = 0; y < tinted.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(tinted.scanLine(y));
        for (int x = 0; x < tinted.width(); ++x) {
            const int l = qGray(line[x]);
            line[x] = qRgba(255, 244, 222, l);
        }
    }
    p.drawImage(QRectF((size - tinted.width()) / 2.0, (size - tinted.height()) / 2.0, tinted.width(), tinted.height()), tinted);
    p.setClipping(false);
    p.setPen(QPen(QColor(0x4a, 0x52, 0x60), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(0.5, 0.5, size - 1, size - 1));
    p.end();
    cache.insert(key, pm);
    return pm;
}

} // namespace lumora
