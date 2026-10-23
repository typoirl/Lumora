// SPDX-License-Identifier: GPL-3.0-or-later
#include "images.h"

#include <QCoreApplication>
#include <QJsonValue>

namespace lumora {

namespace {
QByteArray icon(const char *body)
{
    return QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64' fill='none' stroke='#ffffff' "
                      "stroke-width='4' stroke-linecap='round' stroke-linejoin='round'>")
           + body + "</svg>";
}
} // namespace

bool PoolImage::dataIsSvg(const QByteArray &b)
{
    return b.left(512).contains("<svg") || b.trimmed().startsWith("<?xml");
}

QString PoolImage::displayName() const
{
    return name.isEmpty() ? QCoreApplication::translate("lumora::PoolImage", "Image %1").arg(id) : name;
}

QJsonObject PoolImage::toJson() const
{
    QJsonObject o{{"id", id}, {"name", name}, {"data", QString::fromLatin1(data.toBase64())}};
    if (!file.isEmpty())
        o["file"] = file;
    if (!builtin.isEmpty())
        o["builtin"] = builtin;
    if (tintable)
        o["tintable"] = true;
    return o;
}

PoolImage PoolImage::fromJson(const QJsonObject &o)
{
    PoolImage p;
    p.id = o["id"].toInt();
    p.name = o["name"].toString();
    p.data = QByteArray::fromBase64(o["data"].toString().toLatin1());
    p.file = o["file"].toString();
    p.builtin = o["builtin"].toString();
    p.tintable = o["tintable"].toBool(false);
    return p;
}

QVector<PoolImage> defaultPoolImages()
{
    struct Def {
        QString key;
        QString name;
        QByteArray svg;
    };
    const QVector<Def> defs = {
        {QStringLiteral("arrow"), QObject::tr("Arrow"), icon("<path d='M8 32h44M38 18l14 14-14 14'/>")},
        {QStringLiteral("truss"), QObject::tr("Truss"), icon("<path d='M4 24h56M4 40h56M4 24l8 16 8-16 8 16 8-16 8 16 8-16 8 16'/>")},
        {QStringLiteral("stage"), QObject::tr("Stage"), icon("<rect x='6' y='14' width='52' height='36' rx='2'/><path d='M6 50h52'/>")},
        {QStringLiteral("speaker"), QObject::tr("Speaker"), icon("<rect x='16' y='6' width='32' height='52' rx='4'/><circle cx='32' cy='38' r='9'/><circle cx='32' cy='18' r='4'/>")},
        {QStringLiteral("person"), QObject::tr("Person"), icon("<circle cx='32' cy='14' r='8'/><path d='M18 58V36a14 14 0 0 1 28 0v22'/>")},
        {QStringLiteral("star"), QObject::tr("Star"), icon("<path d='M32 6l7.6 16.4L58 24.6 44.6 37.2 48 56 32 46.8 16 56l3.4-18.8L6 24.6l18.4-2.2z'/>")},
        {QStringLiteral("circle"), QObject::tr("Circle"), icon("<circle cx='32' cy='32' r='24'/>")},
        {QStringLiteral("square"), QObject::tr("Square"), icon("<rect x='10' y='10' width='44' height='44' rx='2'/>")},
        {QStringLiteral("triangle"), QObject::tr("Triangle"), icon("<path d='M32 8l26 46H6z'/>")},
        {QStringLiteral("door"), QObject::tr("Door"), icon("<path d='M14 58V6h36v52M8 58h48'/><circle cx='42' cy='34' r='2.5'/>")},
        {QStringLiteral("camera"), QObject::tr("Camera"), icon("<rect x='6' y='18' width='38' height='28' rx='3'/><path d='M44 26l14-8v28l-14-8'/>")},
        {QStringLiteral("fog"), QObject::tr("Fog"), icon("<path d='M8 26c8-8 16 8 24 0s16 8 24 0M8 40c8-8 16 8 24 0s16 8 24 0'/>")},
        {QStringLiteral("moving-wash"), QObject::tr("Moving wash"), icon("<rect x='18' y='54' width='28' height='6' rx='2'/><path d='M24 54v-4h16v4M10 44V18h6M54 44V18h-6M10 44h44'/>"
                                           "<circle cx='32' cy='28' r='15'/><circle cx='32' cy='28' r='3' fill='#ffffff'/>"
                                           "<circle cx='32' cy='20' r='2.5' fill='#ffffff'/><circle cx='32' cy='36' r='2.5' fill='#ffffff'/>"
                                           "<circle cx='25' cy='24' r='2.5' fill='#ffffff'/><circle cx='39' cy='24' r='2.5' fill='#ffffff'/>"
                                           "<circle cx='25' cy='32' r='2.5' fill='#ffffff'/><circle cx='39' cy='32' r='2.5' fill='#ffffff'/>")},
        {QStringLiteral("moving-profile"), QObject::tr("Moving profile"), icon("<rect x='18' y='54' width='28' height='6' rx='2'/><path d='M24 54v-4h16v4M10 44V16h6M54 44V16h-6M10 44h44'/>"
                                              "<rect x='16' y='8' width='32' height='36' rx='9'/><circle cx='32' cy='26' r='11'/>"
                                              "<circle cx='32' cy='26' r='5'/>")},
        {QStringLiteral("conventional-profile"), QObject::tr("Conventional profile"), icon("<path d='M6 22h10v16H6z'/><path d='M8 22v-3M11 22v-3M14 22v-3M8 38v3M11 38v3M14 38v3'/>"
                                                    "<path d='M16 20l10-6h8v32h-8l-10-6z'/><path d='M34 22h20v16H34z'/>"
                                                    "<path d='M54 20h4v20h-4z'/><path d='M38 22v-5M44 22v-5M38 38v5M44 38v5'/>"
                                                    "<path d='M24 46v8h16v-8M32 54v6M26 60h12'/>")},
    };
    QVector<PoolImage> out;
    for (int i = 0; i < defs.size(); ++i) {
        PoolImage p;
        p.id = i + 1;
        p.name = defs[i].name;
        p.data = defs[i].svg;
        p.builtin = defs[i].key;
        p.tintable = true;
        out << p;
    }
    return out;
}

} // namespace lumora
