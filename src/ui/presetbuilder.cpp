// SPDX-License-Identifier: GPL-3.0-or-later
#include "presetbuilder.h"

#include "appcontext.h"
#include "colorpicker.h"
#include "theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QComboBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumora {

namespace {

// One fixture of each patched type: the mode to work out its values from.
QMap<QString, const FixtureMode *> patchedTypes(Show *show)
{
    QMap<QString, const FixtureMode *> out;
    for (auto it = show->fixtures().begin(); it != show->fixtures().end(); ++it) {
        const Fixture &f = it.value();
        if (f.typeId.isEmpty() || out.contains(f.typeId))
            continue;
        if (const FixtureMode *m = show->fixtureMode(it.key()))
            out.insert(f.typeId, m);
    }
    return out;
}

// A wheel's slots, in order: what each one is called and its middle value. Open, shakes,
// rotation and scrolling are left out.
QVector<QPair<QString, double>> wheelSlots(const ChannelDef *c)
{
    QVector<QPair<QString, double>> out;
    if (!c)
        return out;
    static const QStringList skip = {"open", "shak", "rotat", "scroll", "spin", "bounce", "index", "no function", "nofunction",
                                     "stop", "random", "reverse", "cycle"};
    for (const ChannelSet &s : c->sets) {
        const QString n = s.name.toLower();
        bool skipIt = n.isEmpty();
        for (const QString &k : skip)
            skipIt = skipIt || n.contains(k);
        if (!skipIt)
            out.append({s.name, (s.from + s.to) / 2.0});
    }
    return out;
}

// "Gobo 3 (sailboats)" -> "Sailboats"; anything else as it is.
QString slotLabel(const QString &raw)
{
    const int open = raw.indexOf('('), close = raw.lastIndexOf(')');
    QString s = open >= 0 && close > open ? raw.mid(open + 1, close - open - 1) : raw;
    s = s.trimmed();
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

// The middle of the first range whose name has `word` ("insert", "frost"), for prisms and the like.
double setValue(const ChannelDef *c, const QStringList &words, double fallback)
{
    if (!c)
        return fallback;
    for (const ChannelSet &s : c->sets)
        for (const QString &w : words)
            if (s.name.contains(w, Qt::CaseInsensitive))
                return (s.from + s.to) / 2.0;
    return fallback;
}

struct ColorItem {
    QString name;
    LinearRgb color;
    QColor display;
    double kelvin = 0;   // a color temperature (warm/cool whites blend for it)
};

class PresetBuilderDialog : public QDialog {
public:
    PresetBuilderDialog(AppContext *ctx, QWidget *parent) : QDialog(parent), m_ctx(ctx)
    {
        setWindowTitle(tr("Preset builder"));
        Show *show = ctx->show;
        m_types = patchedTypes(show);
        bool anyGobo = false, anyPrism = false, anyFrost = false, anyIris = false, anyZoom = false;
        for (const FixtureMode *m : std::as_const(m_types)) {
            anyGobo |= !wheelSlots(m->channel("Gobo1")).isEmpty();
            anyPrism |= m->channel("Prism1") != nullptr;
            anyFrost |= m->channel("Frost1") != nullptr;
            anyIris |= m->channel("Iris") != nullptr;
            anyZoom |= m->channel("Zoom") != nullptr;
        }

        auto *v = new QVBoxLayout(this);
        auto *intro = new QLabel(m_types.isEmpty()
                                     ? tr("No fixtures are patched: dimmer, color and zoom presets can still be made (they'll work on "
                                          "fixtures you patch later), and gobo presets as blanks to fill in.")
                                     : tr("Presets for the %n fixture type(s) in the patch, and universal values for anything patched later.",
                                          nullptr, int(m_types.size())),
                                 this);
        intro->setWordWrap(true);
        intro->setProperty("role", "muted");
        v->addWidget(intro);

        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *body = new QWidget(scroll);
        auto *bv = new QVBoxLayout(body);
        bv->setContentsMargins(0, 0, 6, 0);
        scroll->setWidget(body);
        v->addWidget(scroll, 1);

        // Dimmer
        m_dimmer = new QGroupBox(tr("Dimmer"), body);
        m_dimmer->setCheckable(true);
        auto *dg = new QHBoxLayout(m_dimmer);
        for (int level : {100, 90, 75, 50, 25, 10, 0}) {
            auto *c = new QCheckBox(level == 100 ? tr("Full") : level == 0 ? tr("Out") : QStringLiteral("%1%").arg(level), m_dimmer);
            c->setChecked(level == 100 || level == 75 || level == 50 || level == 25 || level == 0);
            c->setProperty("level", level);
            dg->addWidget(c);
            m_levels << c;
        }
        dg->addStretch(1);
        bv->addWidget(m_dimmer);

        // Color
        m_colorBox = new QGroupBox(tr("Color"), body);
        m_colorBox->setCheckable(true);
        auto *cg = new QVBoxLayout(m_colorBox);
        m_colors = new QListWidget(m_colorBox);
        m_colors->setMinimumHeight(170);
        m_colors->setViewMode(QListView::ListMode);
        m_colors->setFlow(QListView::LeftToRight);
        m_colors->setWrapping(true);
        m_colors->setResizeMode(QListView::Adjust);
        m_colors->setGridSize(QSize(150, 26));
        m_colors->setMovement(QListView::Static);
        for (const Swatch &sw : standardColors())
            addColor({sw.name, toLinear(sw.color), sw.color, 0}, true);
        const LinearRgb warm = kelvinToLinear(3200);
        addColor({QStringLiteral("3200K"), warm, toDisplay(warm), 3200}, true);
        cg->addWidget(m_colors);
        auto *more = new QHBoxLayout;
        auto *gels = new QPushButton(tr("Add gels…"), m_colorBox);
        auto *temps = new QPushButton(tr("Add color temperatures"), m_colorBox);
        auto *none = new QPushButton(tr("Untick all"), m_colorBox);
        more->addWidget(gels);
        more->addWidget(temps);
        more->addStretch(1);
        more->addWidget(none);
        cg->addLayout(more);
        connect(gels, &QPushButton::clicked, this, [this]() { pickGels(); });
        connect(temps, &QPushButton::clicked, this, [this]() {
            const QList<QPair<int, QString>> list = {{1900, tr("Candle")}, {2700, tr("Warm white")}, {3200, tr("Tungsten")},
                                                     {4300, tr("Moonlight")}, {5600, tr("Daylight")}, {6500, tr("Overcast")},
                                                     {8000, tr("Blue sky")}, {10000, tr("Deep sky")}};
            for (const auto &t : list) {
                const LinearRgb c = kelvinToLinear(t.first);
                addColor({QStringLiteral("%1K").arg(t.first), c, toDisplay(c), double(t.first)}, true);
            }
            refreshSummary();
        });
        connect(none, &QPushButton::clicked, this, [this]() {
            for (int i = 0; i < m_colors->count(); ++i)
                m_colors->item(i)->setCheckState(Qt::Unchecked);
            refreshSummary();
        });
        connect(m_colors, &QListWidget::itemChanged, this, [this]() { refreshSummary(); });
        bv->addWidget(m_colorBox);

        // Gobo
        m_goboBox = new QGroupBox(tr("Gobo"), body);
        m_goboBox->setCheckable(true);
        m_goboBox->setChecked(anyGobo);
        auto *gg = new QGridLayout(m_goboBox);
        m_fromWheels = new QRadioButton(tr("From the patched fixtures' gobo wheels"), m_goboBox);
        m_wheel2 = new QCheckBox(tr("Wheel 2 too"), m_goboBox);
        m_wheel2->setChecked(true);
        m_blank = new QRadioButton(tr("Blank, labeled Gobo 1, Gobo 2…"), m_goboBox);
        m_blankCount = new QSpinBox(m_goboBox);
        m_blankCount->setRange(1, 50);
        m_blankCount->setValue(8);
        m_fromWheels->setEnabled(anyGobo);
        m_wheel2->setEnabled(anyGobo);
        (anyGobo ? m_fromWheels : m_blank)->setChecked(true);
        gg->addWidget(m_fromWheels, 0, 0);
        gg->addWidget(m_wheel2, 0, 1);
        gg->addWidget(m_blank, 1, 0);
        gg->addWidget(m_blankCount, 1, 1);
        if (!anyGobo) {
            auto *note = new QLabel(tr("No patched fixture has a gobo wheel."), m_goboBox);
            note->setProperty("role", "muted");
            gg->addWidget(note, 2, 0, 1, 2);
        }
        bv->addWidget(m_goboBox);

        // Beam
        m_beamBox = new QGroupBox(tr("Beam"), body);
        m_beamBox->setCheckable(true);
        auto *beg = new QVBoxLayout(m_beamBox);
        m_zoom = new QCheckBox(tr("Zoom: Narrow, Medium, Wide"), m_beamBox);
        m_zoom->setChecked(true);
        m_prism = new QCheckBox(tr("Prism in, Prism out"), m_beamBox);
        m_frost = new QCheckBox(tr("Frost in, Frost out"), m_beamBox);
        m_iris = new QCheckBox(tr("Iris open, Iris tight"), m_beamBox);
        m_prism->setEnabled(anyPrism);
        m_prism->setChecked(anyPrism);
        m_frost->setEnabled(anyFrost);
        m_frost->setChecked(anyFrost);
        m_iris->setEnabled(anyIris);
        m_iris->setChecked(anyIris);
        if (!anyZoom && !m_types.isEmpty())
            m_zoom->setToolTip(tr("No patched fixture has a zoom; these are made for fixtures patched later."));
        for (QCheckBox *c : {m_zoom, m_prism, m_frost, m_iris})
            beg->addWidget(c);
        if (m_types.isEmpty()) {
            auto *note = new QLabel(tr("Prism, frost and iris need fixtures that have them."), m_beamBox);
            note->setProperty("role", "muted");
            beg->addWidget(note);
        }
        bv->addWidget(m_beamBox);
        bv->addStretch(1);

        // Where
        auto *where = new QHBoxLayout;
        auto *whereLabel = new QLabel(tr("Put them"), this);
        m_startMode = new QComboBox(this);
        m_startMode->addItems({tr("After what's in each pool"), tr("From slot, skipping used slots"), tr("From slot, replacing what's there")});
        m_startAt = new QSpinBox(this);
        m_startAt->setRange(1, 9999);
        m_startAt->setValue(1);
        m_startAt->setPrefix(tr("slot "));
        m_startAt->setEnabled(false);
        connect(m_startMode, &QComboBox::currentIndexChanged, this, [this](int i) { m_startAt->setEnabled(i > 0); });
        where->addWidget(whereLabel);
        where->addWidget(m_startMode);
        where->addWidget(m_startAt);
        where->addStretch(1);
        v->addLayout(where);
        m_summary = new QLabel(this);
        m_summary->setProperty("role", "muted");
        v->addWidget(m_summary);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        auto *make = buttons->addButton(tr("Make presets"), QDialogButtonBox::AcceptRole);
        make->setProperty("role", "primary");
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            build();
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        v->addWidget(buttons);
        for (QGroupBox *g : {m_dimmer, m_colorBox, m_goboBox, m_beamBox})
            connect(g, &QGroupBox::toggled, this, [this]() { refreshSummary(); });
        for (QAbstractButton *b : std::initializer_list<QAbstractButton *>{m_fromWheels, m_blank, m_wheel2, m_zoom, m_prism, m_frost, m_iris})
            connect(b, &QAbstractButton::toggled, this, [this]() { refreshSummary(); });
        for (QCheckBox *c : std::as_const(m_levels))
            connect(c, &QCheckBox::toggled, this, [this]() { refreshSummary(); });
        connect(m_blankCount, &QSpinBox::valueChanged, this, [this]() { refreshSummary(); });
        resize(720, 640);
        refreshSummary();
    }

private:
    void addColor(const ColorItem &c, bool checked)
    {
        for (const ColorItem &have : std::as_const(m_colorItems))
            if (have.name == c.name)
                return;
        QPixmap chip(14, 14);
        chip.fill(c.display);
        auto *it = new QListWidgetItem(QIcon(chip), c.name, m_colors);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        it->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        it->setData(Qt::UserRole, int(m_colorItems.size()));
        m_colorItems << c;
    }

    void pickGels()
    {
        QDialog d(this);
        d.setWindowTitle(tr("Add gels"));
        auto *v = new QVBoxLayout(&d);
        auto *search = new QLineEdit(&d);
        search->setPlaceholderText(tr("Search: a number (R26, L201) or a name"));
        search->setClearButtonEnabled(true);
        v->addWidget(search);
        auto *list = new QListWidget(&d);
        list->setSelectionMode(QAbstractItemView::MultiSelection);
        for (const Swatch &g : gelLibrary()) {
            QPixmap chip(14, 14);
            chip.fill(g.color);
            auto *it = new QListWidgetItem(QIcon(chip), QStringLiteral("%1  %2  (%3)").arg(g.code, g.name, g.maker), list);
            it->setData(Qt::UserRole, g.code);
        }
        v->addWidget(list, 1);
        connect(search, &QLineEdit::textChanged, &d, [list](const QString &q) {
            for (int i = 0; i < list->count(); ++i)
                list->item(i)->setHidden(!q.isEmpty() && !list->item(i)->text().contains(q, Qt::CaseInsensitive));
        });
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        bb->button(QDialogButtonBox::Ok)->setText(tr("Add the selected gels"));
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        v->addWidget(bb);
        d.resize(460, 520);
        if (d.exec() != QDialog::Accepted)
            return;
        for (QListWidgetItem *it : list->selectedItems())
            for (const Swatch &g : gelLibrary())
                if (g.code == it->data(Qt::UserRole).toString())
                    addColor({QStringLiteral("%1 %2").arg(g.code, g.name), toLinear(g.color), g.color, 0}, true);
        refreshSummary();
    }

    QVector<ColorItem> chosenColors() const
    {
        QVector<ColorItem> out;
        for (int i = 0; i < m_colors->count(); ++i)
            if (m_colors->item(i)->checkState() == Qt::Checked)
                out << m_colorItems[m_colors->item(i)->data(Qt::UserRole).toInt()];
        return out;
    }

    // The gobo presets to make: a name and, per fixture type, the wheel value.
    QVector<QPair<QString, QMap<QString, RawAttr>>> goboPlan() const
    {
        QVector<QPair<QString, QMap<QString, RawAttr>>> out;
        if (!m_goboBox->isChecked())
            return out;
        if (m_blank->isChecked()) {
            for (int i = 1; i <= m_blankCount->value(); ++i)
                out.append({tr("Gobo %1").arg(i), {}});
            return out;
        }
        for (const char *wheel : {"Gobo1", "Gobo2"}) {
            if (QLatin1String(wheel) == QLatin1String("Gobo2") && !m_wheel2->isChecked())
                break;
            int most = 0;
            QMap<QString, QVector<QPair<QString, double>>> perType;
            for (auto it = m_types.begin(); it != m_types.end(); ++it) {
                const auto wheelSet = wheelSlots(it.value()->channel(wheel));
                if (!wheelSet.isEmpty())
                    perType.insert(it.key(), wheelSet);
                most = std::max(most, int(wheelSet.size()));
            }
            for (int i = 0; i < most; ++i) {
                QMap<QString, RawAttr> values;
                QString name;
                for (auto it = perType.begin(); it != perType.end(); ++it)
                    if (i < it->size()) {
                        values[it.key()].insert(wheel, it->at(i).second);
                        if (name.isEmpty())
                            name = slotLabel(it->at(i).first);
                    }
                // One type: the slot's own name; several: numbered by wheel.
                if (perType.size() != 1)
                    name = QLatin1String(wheel) == QLatin1String("Gobo1") ? tr("Gobo %1").arg(i + 1) : tr("Gobo 2.%1").arg(i + 1);
                out.append({name, values});
            }
        }
        return out;
    }

    int levelsChosen() const
    {
        int n = 0;
        for (QCheckBox *c : m_levels)
            n += c->isChecked();
        return m_dimmer->isChecked() ? n : 0;
    }
    int beamCount() const
    {
        if (!m_beamBox->isChecked())
            return 0;
        return (m_zoom->isChecked() ? 3 : 0) + (m_prism->isChecked() && m_prism->isEnabled() ? 2 : 0)
               + (m_frost->isChecked() && m_frost->isEnabled() ? 2 : 0) + (m_iris->isChecked() && m_iris->isEnabled() ? 2 : 0);
    }

    void refreshSummary()
    {
        const int colors = m_colorBox->isChecked() ? int(chosenColors().size()) : 0;
        m_summary->setText(tr("Makes %1 dimmer, %2 color, %3 gobo and %4 beam presets.")
                               .arg(levelsChosen()).arg(colors).arg(goboPlan().size()).arg(beamCount()));
    }

    int nextSlot(int type, int &cursor) const
    {
        // After what's in the pool, or from a slot you pick (skipping used slots, or replacing them).
        const int mode = m_startMode->currentIndex();
        if (cursor == 0) {
            const QMap<int, Preset> &pool = m_ctx->show->presets(type);
            cursor = mode > 0 ? m_startAt->value() : !pool.isEmpty() ? pool.lastKey() + 1 : 1;
        }
        if (mode != 2)
            while (m_ctx->show->preset(type, cursor))
                ++cursor;
        return cursor++;
    }

    void build()
    {
        Show *show = m_ctx->show;
        int made = 0;
        auto store = [&](int type, int &cursor, const QString &name, const RawAttr &universal, const QMap<QString, RawAttr> &perType) {
            Preset p;
            p.type = type;
            p.id = nextSlot(type, cursor);
            p.name = name;
            p.universal = universal;
            p.perType = perType;
            show->setPreset(p);
            ++made;
        };
        if (m_dimmer->isChecked()) {
            int cursor = 0;
            for (QCheckBox *c : std::as_const(m_levels))
                if (c->isChecked())
                    store(int(Feature::Dimmer), cursor, c->text(), {{"Dimmer", c->property("level").toInt() / 100.0}}, {});
        }
        if (m_colorBox->isChecked()) {
            int cursor = 0;
            for (const ColorItem &ci : chosenColors()) {
                // Universal: any RGB(W) or CMY fixture; then each patched type's own values.
                const double peak = std::max({ci.color.r, ci.color.g, ci.color.b, 1e-6});
                const double r = ci.color.r / peak, g = ci.color.g / peak, b = ci.color.b / peak;
                const RawAttr universal = {{"ColorAdd_R", r}, {"ColorAdd_G", g}, {"ColorAdd_B", b}, {"ColorAdd_W", std::min({r, g, b})},
                                           {"ColorSub_C", 1 - r}, {"ColorSub_M", 1 - g}, {"ColorSub_Y", 1 - b}};
                QMap<QString, RawAttr> perType;
                for (auto it = m_types.begin(); it != m_types.end(); ++it) {
                    const RawAttr values = ci.kelvin > 0 ? temperatureValuesFor(it.value(), ci.kelvin)
                                                         : colorValuesFor(it.value(), ci.color, ci.display, ci.name);
                    if (!values.isEmpty())
                        perType.insert(it.key(), values);
                }
                store(int(Feature::Color), cursor, ci.name, universal, perType);
            }
        }
        {
            int cursor = 0;
            for (const auto &g : goboPlan())
                store(int(Feature::Gobo), cursor, g.first, {}, g.second);
        }
        if (m_beamBox->isChecked()) {
            int cursor = 0;
            if (m_zoom->isChecked()) {
                // Narrow to wide in each type's own direction (some zooms run wide to narrow).
                const QPair<QString, double> zooms[] = {{tr("Narrow"), 0.0}, {tr("Medium"), 0.5}, {tr("Wide"), 1.0}};
                for (const auto &z : zooms) {
                    QMap<QString, RawAttr> perType;
                    for (auto it = m_types.begin(); it != m_types.end(); ++it)
                        if (const ChannelDef *c = it.value()->channel("Zoom"))
                            perType[it.key()].insert("Zoom", c->rangeFrom() > c->rangeTo() ? 1.0 - z.second : z.second);
                    store(int(Feature::Beam), cursor, z.first, {{"Zoom", z.second}}, perType);
                }
            }
            auto pair = [&](QCheckBox *box, const char *attr, const QString &inName, const QString &outName, const QStringList &words,
                            double outValue) {
                if (!box->isChecked() || !box->isEnabled())
                    return;
                QMap<QString, RawAttr> in, out;
                for (auto it = m_types.begin(); it != m_types.end(); ++it)
                    if (const ChannelDef *c = it.value()->channel(attr)) {
                        in[it.key()].insert(attr, setValue(c, words, 1.0));
                        out[it.key()].insert(attr, outValue);
                    }
                store(int(Feature::Beam), cursor, inName, {}, in);
                store(int(Feature::Beam), cursor, outName, {}, out);
            };
            pair(m_prism, "Prism1", tr("Prism in"), tr("Prism out"), {QStringLiteral("insert"), QStringLiteral("prism")}, 0.0);
            pair(m_frost, "Frost1", tr("Frost in"), tr("Frost out"), {QStringLiteral("frost"), QStringLiteral("insert")}, 0.0);
            pair(m_iris, "Iris", tr("Iris tight"), tr("Iris open"), {QStringLiteral("close")}, 0.0);
        }
        emit m_ctx->status(tr("Preset builder: %n preset(s) made", nullptr, made));
    }

    AppContext *m_ctx;
    QMap<QString, const FixtureMode *> m_types;
    QGroupBox *m_dimmer, *m_colorBox, *m_goboBox, *m_beamBox;
    QList<QCheckBox *> m_levels;
    QListWidget *m_colors;
    QVector<ColorItem> m_colorItems;
    QRadioButton *m_fromWheels, *m_blank;
    QCheckBox *m_wheel2;
    QSpinBox *m_blankCount;
    QCheckBox *m_zoom, *m_prism, *m_frost, *m_iris;
    QComboBox *m_startMode;
    QSpinBox *m_startAt;
    QLabel *m_summary;
};

} // namespace

void openPresetBuilder(AppContext *ctx, QWidget *parent)
{
    PresetBuilderDialog d(ctx, parent);
    d.exec();
}

} // namespace lumora
