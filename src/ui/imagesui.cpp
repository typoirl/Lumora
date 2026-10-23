// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagesui.h"

#include "appcontext.h"
#include "plotsui.h"
#include "quickedit.h"
#include "theme.h"
#include "uiutil.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace lumora {

namespace {
const QString kFilter = QObject::tr("Pictures (*.svg *.png *.jpg *.jpeg *.bmp *.gif)");

QImage thumbnail(const PoolImage &img, const QSize &size)
{
    return PlotCanvas::renderImage(img.data, size, img.tintable ? theme::text.name() : QString()).toImage();
}
} // namespace

ImagesPanel::ImagesPanel(AppContext *ctx, QWidget *parent) : PoolPanel(tr("Images"), parent), m_ctx(ctx)
{
    setAcceptDrops(true);
    Show *show = ctx->show;
    connect(tiles(), &PoolTiles::tileClicked, this, [this, show](int id, bool occupied, Qt::KeyboardModifiers) {
        if (m_ctx->storeArmed()) {   // Store, then a slot: import into it
            m_ctx->setStoreArmed(false);
            importInto(id, true);
            return;
        }
        if (m_ctx->tapTarget(QStringLiteral("Image %1").arg(id), occupied ? show->image(id)->name : QString()))
            return;
        if (!occupied) {
            importInto(id, false);
            return;
        }
        const int plot = m_ctx->uiValue("plot", 0).toInt();
        emit m_ctx->status(plot > 0 ? tr("Image %1: %2. Right-click it to put it on Plot %3").arg(id).arg(show->image(id)->displayName()).arg(plot)
                                    : tr("Image %1: %2. Right-click it to put it on a plot").arg(id).arg(show->image(id)->displayName()));
    });
    connect(tiles(), &PoolTiles::tileMenu, this, [this, show](int id, bool occupied, const QPoint &pos) {
        auto *ed = new QuickEditor(occupied ? show->image(id)->displayName() : tr("Image %1").arg(id), this);
        if (!occupied) {
            ed->addAction(tr("Import pictures…"), [this, id]() { importInto(id, false); });
            ed->popup(pos);
            return;
        }
        const PoolImage img = *show->image(id);
        ed->addNameField(tr("Name"), img.name, [this, id](const QString &n) {
            QString q = n.trimmed();
            q.replace('"', '\'');
            m_ctx->run(QStringLiteral("Label Image %1 \"%2\"").arg(id).arg(q));
        }, true, tr("Image %1").arg(id));
        ed->addSection(QString());
        const int plot = m_ctx->uiValue("plot", 0).toInt();
        if (plot > 0 && show->plot(plot)) {
            ed->addAction(tr("Put it on Plot %1").arg(plot), [this, id, plot]() {
                m_ctx->run(QStringLiteral("Assign Image %1 At Plot %2").arg(id).arg(plot));
            });
            ed->addAction(tr("Make it Plot %1's background").arg(plot), [this, show, id, plot]() {
                const Plot *cur = show->plot(plot);
                if (!cur)
                    return;
                Plot p = *cur;
                QRectF b = p.bounds();
                if (b.isNull())
                    b = QRectF(0, 0, 800, 600);
                p.setBackgroundImage(id, b);
                show->setPlot(p);
            });
        }
        ed->addAction(tr("Replace with a file…"), [this, id]() { importInto(id, true); });
        ed->addAction(tr("Export…"), [this, img]() {
            const QString ext = img.isSvg() ? QStringLiteral("svg") : img.data.startsWith("\x89PNG") ? QStringLiteral("png")
                                : img.data.startsWith("GIF8") ? QStringLiteral("gif") : img.data.startsWith("BM") ? QStringLiteral("bmp")
                                                                                                                    : QStringLiteral("jpg");
            const QString path = QFileDialog::getSaveFileName(this, tr("Export image"), img.displayName() + '.' + ext);
            if (path.isEmpty())
                return;
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(img.data);
        });
        const int uses = show->imageUses(id);
        ed->addAction(uses ? tr("Delete (used %n time(s) on plots)", nullptr, uses) : tr("Delete"), [this, id, uses]() {
            if (uses && QMessageBox::question(this, tr("Delete image"),
                                              tr("Plots use Image %1 %n time(s). Delete it anyway? They'll show it as missing.", nullptr, uses).arg(id))
                            != QMessageBox::Yes)
                return;
            m_ctx->run(QStringLiteral("Delete Image %1").arg(id));
        }, QIcon(), true);
        ed->popup(pos);
    });
    onShowChanged(this, show, [this](Show::Parts p) {
        if (p & (Show::ImagesPart | Show::PlotsPart))
            refresh();
    });
    followStore(ctx);
    refresh();
}

void ImagesPanel::refresh()
{
    QVector<PoolItem> items;
    for (const PoolImage &img : m_ctx->show->images()) {
        PoolItem it;
        it.id = img.id;
        it.label = img.displayName();
        it.tag = theme::muted;
        it.image = thumbnail(img, QSize(128, 128));
        it.picture = true;
        const int uses = m_ctx->show->imageUses(img.id);
        QString tip = img.file.isEmpty() ? img.displayName() : QStringLiteral("%1 (%2)").arg(img.displayName(), img.file);
        if (uses)
            tip += tr("\nUsed %n time(s) on plots", nullptr, uses);
        it.tooltip = tip;
        items << it;
    }
    tiles()->setItems(items);
}

int ImagesPanel::importFiles(const QStringList &paths, int firstSlot, bool replaceFirst)
{
    Show *show = m_ctx->show;
    int slot = firstSlot, n = 0;
    QStringList problems;
    for (int i = 0; i < paths.size(); ++i) {
        if (!(i == 0 && replaceFirst))
            slot = show->nextFreeImage(slot);
        QString error;
        if (show->importImage(paths[i], slot, &error)) {
            ++n;
            ++slot;
        } else {
            problems << error;
        }
    }
    if (!problems.isEmpty())
        emit m_ctx->status(problems.join(QStringLiteral("; ")));
    else if (n)
        emit m_ctx->status(tr("%n picture(s) imported into the image pool", nullptr, n));
    return n;
}

void ImagesPanel::importInto(int firstSlot, bool replace)
{
    if (replace) {
        const QString path = QFileDialog::getOpenFileName(this, tr("Image %1").arg(firstSlot), QString(), kFilter);
        if (!path.isEmpty())
            importFiles({path}, firstSlot, true);
        return;
    }
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Import pictures"), QString(), kFilter);
    if (!paths.isEmpty())
        importFiles(paths, firstSlot, false);
}

void ImagesPanel::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls())
        ev->acceptProposedAction();
}

void ImagesPanel::dropEvent(QDropEvent *ev)
{
    QStringList paths;
    for (const QUrl &u : ev->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    if (paths.isEmpty())
        return;
    importFiles(paths, 1, false);
    ev->acceptProposedAction();
}

int pickPoolImage(AppContext *ctx, QWidget *parent, int current)
{
    QDialog d(parent);
    d.setWindowTitle(QObject::tr("Image pool"));
    auto *v = new QVBoxLayout(&d);
    auto *list = new QListWidget(&d);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(64, 64));
    list->setGridSize(QSize(104, 104));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    for (const PoolImage &img : ctx->show->images()) {
        auto *it = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnail(img, QSize(64, 64)))),
                                       QStringLiteral("%1 %2").arg(img.id).arg(img.displayName()), list);
        it->setData(Qt::UserRole, img.id);
        if (img.id == current)
            list->setCurrentItem(it);
    }
    v->addWidget(list, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Cancel, &d);
    auto *import = bb->addButton(QObject::tr("Import a picture…"), QDialogButtonBox::ActionRole);
    auto *ok = bb->addButton(QObject::tr("Use it"), QDialogButtonBox::AcceptRole);
    ok->setProperty("role", "primary");
    v->addWidget(bb);
    int chosen = 0;
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &d, &QDialog::accept);
    QObject::connect(import, &QPushButton::clicked, &d, [&]() {
        const QString path = QFileDialog::getOpenFileName(&d, QObject::tr("Import a picture"), QString(), kFilter);
        if (path.isEmpty())
            return;
        QString error;
        const int id = ctx->show->importImage(path, 0, &error);
        if (!id) {
            QMessageBox::warning(&d, QObject::tr("Import a picture"), error);
            return;
        }
        chosen = id;
        d.accept();
    });
    d.resize(560, 460);
    if (d.exec() != QDialog::Accepted)
        return 0;
    if (chosen)
        return chosen;
    return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : 0;
}

} // namespace lumora
