// SPDX-License-Identifier: LGPL-2.1-or-later

 /****************************************************************************
  *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>               *
  *   Copyright (c) 2023 FreeCAD Project Association                         *
  *                                                                          *
  *   This file is part of FreeCAD.                                          *
  *                                                                          *
  *   FreeCAD is free software: you can redistribute it and/or modify it     *
  *   under the terms of the GNU Lesser General Public License as            *
  *   published by the Free Software Foundation, either version 2.1 of the   *
  *   License, or (at your option) any later version.                        *
  *                                                                          *
  *   FreeCAD is distributed in the hope that it will be useful, but         *
  *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
  *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
  *   Lesser General Public License for more details.                        *
  *                                                                          *
  *   You should have received a copy of the GNU Lesser General Public       *
  *   License along with FreeCAD. If not, see                                *
  *   <https://www.gnu.org/licenses/>.                                       *
  *                                                                          *
  ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <algorithm>
# include <cstring>
# include <QAbstractButton>
# include <QApplication>
# include <QCursor>
# include <QDebug>
# include <QMenu>
# include <QMessageBox>
# include <QScreen>
# include <QScrollArea>
# include <QScrollBar>
# include <QTimer>
# include <QToolTip>
# include <QProcess>
# include <QPushButton>
#endif

#include <App/Application.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Tools.h>

#include "DlgPreferencesImp.h"
#include "ui_DlgPreferences.h"
#include "BitmapFactory.h"
#include "MainWindow.h"
#include "Tools.h"
#include "WidgetFactory.h"


using namespace Gui::Dialog;

QWidget* PreferencesPageItem::getWidget() const {
    return _widget;
}

void PreferencesPageItem::setWidget(QWidget* widget)
{
    if (_widget) {
        _widget->setProperty(PropertyName, QVariant::fromValue<PreferencesPageItem*>(nullptr));
    }
    
    _widget = widget;
    _widget->setProperty(PropertyName, QVariant::fromValue(this));
}

Q_DECLARE_METATYPE(PreferencesPageItem*);

const int DlgPreferencesImp::GroupNameRole = Qt::UserRole + 1;
const int DlgPreferencesImp::PageNameRole = Qt::UserRole + 2;

/* TRANSLATOR Gui::Dialog::DlgPreferencesImp */

std::list<DlgPreferencesImp::TGroupPages> DlgPreferencesImp::_pages;
std::map<std::string, DlgPreferencesImp::Group> DlgPreferencesImp::_groupMap;

DlgPreferencesImp* DlgPreferencesImp::_activeDialog = nullptr;

/**
 *  Constructs a DlgPreferencesImp which is a child of 'parent', with
 *  widget flags set to 'fl'
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgPreferencesImp::DlgPreferencesImp(QWidget* parent, Qt::WindowFlags fl)
    : QDialog(parent, fl), ui(new Ui_DlgPreferences),
      invalidParameter(false), canEmbedScrollArea(true), restartRequired(false)
{
    ui->setupUi(this);

    // remove unused help button
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    connect(ui->buttonBox,
            &QDialogButtonBox::clicked,
            this,
            &DlgPreferencesImp::onButtonBoxClicked);
    connect(ui->buttonBox,
            &QDialogButtonBox::helpRequested,
            getMainWindow(),
            &MainWindow::whatsThis);
    connect(ui->groupsTreeView,
            &QTreeView::clicked,
            this,
            &DlgPreferencesImp::onPageSelected);
    connect(ui->buttonReset,
            &QPushButton::clicked,
            this,
            &DlgPreferencesImp::showResetOptions);

    ui->groupsTreeView->setModel(&_model);
    
    setupPages();

    // Maintain a static pointer to the current active dialog (if there is one) so that
    // if the static page manipulation functions are called while the dialog is showing
    // it can update its content.
    DlgPreferencesImp::_activeDialog = this;
}

/**
 *  Destroys the object and frees any allocated resources.
 */
DlgPreferencesImp::~DlgPreferencesImp()
{
    if (DlgPreferencesImp::_activeDialog == this) {
        DlgPreferencesImp::_activeDialog = nullptr;
    }
}

void DlgPreferencesImp::setupPages()
{
    // make sure that pages are ready to create
    GetWidgetFactorySupplier();

    for (const auto &[name, pages] : _pages) {
        auto* group = createGroup(name);

        for (const auto &page : pages) {
            createPageInGroup(group, page);
        }
    }

    updatePageDependentLabels();
}

QPixmap DlgPreferencesImp::loadIconForGroup(const std::string &name) const
{
    std::string fileName = name;

    // normalize file name
    for (auto& ch : fileName) {
        ch = ch == ' ' ? '_' : tolower(ch);
    }

    fileName = std::string("preferences-") + fileName;
    QPixmap icon = Gui::BitmapFactory().pixmapFromSvg(fileName.c_str(), QSize(24, 24));

    if (icon.isNull()) {
        icon = Gui::BitmapFactory().pixmap(fileName.c_str());
        if (icon.isNull()) {
            qWarning() << "No group icon found for " << fileName.c_str();
        }
        else if (icon.size() != QSize(24, 24)) {
            icon = icon.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    return icon;
}

/**
 * Create the necessary widgets for a new group named \a groupName. Returns a 
 * pointer to the group's SettingsPageItem: that widget's lifetime is managed by the 
 * QStandardItemModel, do not manually deallocate.
 */
PreferencesPageItem* DlgPreferencesImp::createGroup(const std::string &groupName)
{
    QString groupNameQString = QString::fromStdString(groupName);

    std::string iconName;
    
    QString tooltip;
    getGroupData(groupName, iconName, tooltip);

    auto groupPages = new QStackedWidget;
    groupPages->setProperty(GroupNameProperty, QVariant(groupNameQString));

    ui->groupWidgetStack->addWidget(groupPages);

    auto item = new PreferencesPageItem;

    item->setData(QVariant(groupNameQString), GroupNameRole);
    item->setText(QObject::tr(groupNameQString.toLatin1()));
    item->setToolTip(tooltip);
    item->setIcon(loadIconForGroup(iconName));
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    item->setWidget(groupPages);

    _model.invisibleRootItem()->appendRow(item);

    return item;
}


PreferencePage* DlgPreferencesImp::createPreferencePage(const std::string& pageName, const std::string& groupName)
{
    PreferencePage* page = WidgetFactory().createPreferencePage(pageName.c_str());

    if (!page) {
        return nullptr;
    }

    // settings layout already takes care for margins, we need to reset everything to 0
    page->setContentsMargins(0, 0, 0, 0);
    page->layout()->setContentsMargins(0, 0, 0, 0);
    page->setProperty(GroupNameProperty, QString::fromStdString(groupName));
    page->setProperty(PageNameProperty, QString::fromStdString(pageName));

    return page;
}

/**
 * Create a new preference page called \a pageName in the group \a groupItem.
 */
void DlgPreferencesImp::createPageInGroup(PreferencesPageItem *groupItem, const std::string &pageName)
{
    try {
        PreferencePage* page = createPreferencePage(pageName, groupItem->data(GroupNameRole).toString().toStdString());

        if (!page) {
            Base::Console().Warning("%s is not a preference page\n", pageName.c_str());

            return;
        }

        auto pageItem = new PreferencesPageItem;

        pageItem->setText(page->windowTitle());
        pageItem->setEditable(false);
        pageItem->setData(groupItem->data(GroupNameRole), GroupNameRole);
        pageItem->setData(QString::fromStdString(pageName), PageNameRole);
        pageItem->setWidget(page);

        groupItem->appendRow(pageItem);

        page->loadSettings();

        auto pages = qobject_cast<QStackedWidget*>(groupItem->getWidget());
        pages->addWidget(page);
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("Base exception thrown for '%s'\n", pageName.c_str());
        e.ReportException();
    }
    catch (const std::exception& e) {
        Base::Console().Error("C++ exception thrown for '%s' (%s)\n", pageName.c_str(), e.what());
    }
}

void DlgPreferencesImp::updatePageDependentLabels()
{
    auto currentPageItem = getCurrentPage();

    ui->headerLabel->setText(currentPageItem->text());
}

/**
 * Adds a preference page with its class name \a className and
 * the group \a group it belongs to. To create this page it must
 * be registered in the WidgetFactory.
 * @see WidgetFactory
 * @see PrefPageProducer
 */
void DlgPreferencesImp::addPage(const std::string& className, const std::string& group)
{
    auto groupToAddTo = _pages.end();
    for (auto it = _pages.begin(); it != _pages.end(); ++it) {
        if (it->first == group) {
            groupToAddTo = it;
            break;
        }
    }

    if (groupToAddTo != _pages.end()) {
        // The group exists: add this page to the end of the list
        groupToAddTo->second.push_back(className);
    }
    else {
        // This is a new group: create it, with its one page
        std::list<std::string> pages;
        pages.push_back(className);
        _pages.emplace_back(group, pages);
    }

    if (DlgPreferencesImp::_activeDialog) {
        // If the dialog is currently showing, tell it to insert the new page
        _activeDialog->reloadPages();
    }
}

void DlgPreferencesImp::removePage(const std::string& className, const std::string& group)
{
    for (auto it = _pages.begin(); it != _pages.end(); ++it) {
        if (it->first == group) {
            if (className.empty()) {
                _pages.erase(it);
                return;
            }
            else {
                std::list<std::string>& p = it->second;
                for (auto jt = p.begin(); jt != p.end(); ++jt) {
                    if (*jt == className) {
                        p.erase(jt);
                        if (p.empty()) {
                            _pages.erase(it);
                        }
                        return;
                    }
                }
            }
        }
    }
}

/**
 * Sets a custom icon name or tool tip for a given group.
 */
void DlgPreferencesImp::setGroupData(const std::string& name, const std::string& icon, const QString& tip)
{
    Group group;
    group.iconName = icon;
    group.tooltip = tip;
    _groupMap[name] = group;
}

/**
 * Gets the icon name or tool tip for a given group. If no custom name or tool tip is given
 * they are determined from the group name.
 */
void DlgPreferencesImp::getGroupData(const std::string& group, std::string& icon, QString& tip)
{
    auto it = _groupMap.find(group);

    if (it != _groupMap.end()) {
        icon = it->second.iconName;
        tip = it->second.tooltip;
    }

    if (icon.empty()) {
        icon = group;
    }

    if (tip.isEmpty()) {
        tip = QObject::tr(group.c_str());
    }
}

/**
 * Activates the page at position \a index of the group with name \a group.
 */
void DlgPreferencesImp::activateGroupPage(const QString& group, int index)
{
    for (int i = 0; i < ui->groupWidgetStack->count(); i++) {
        auto* pageStackWidget = qobject_cast<QStackedWidget*>(ui->groupWidgetStack->widget(i));

        if (!pageStackWidget) {
            continue;
        }

        if (pageStackWidget->property(GroupNameProperty).toString() == group) {
            ui->groupWidgetStack->setCurrentWidget(pageStackWidget);
            pageStackWidget->setCurrentIndex(index);

            updatePageDependentLabels();
            
            return;
        }
    }
}

/**
 * Returns the group name \a group and position \a index of the active page.
 */
void DlgPreferencesImp::activeGroupPage(QString& group, int& index) const
{
    auto groupWidget = qobject_cast<QStackedWidget*>(ui->groupWidgetStack->currentWidget());

    if (groupWidget) {
        group = groupWidget->property(GroupNameProperty).toString();
        index = groupWidget->currentIndex();
    }
}

void DlgPreferencesImp::accept()
{
    this->invalidParameter = false;

    applyChanges();
    
    if (!this->invalidParameter) {
        QDialog::accept();
        restartIfRequired();
    }
}

void DlgPreferencesImp::reject()
{
    QDialog::reject();
    restartIfRequired();
}

void DlgPreferencesImp::onButtonBoxClicked(QAbstractButton* btn) 
{
    if (ui->buttonBox->standardButton(btn) == QDialogButtonBox::Apply) {
        applyChanges();
    }
}

void DlgPreferencesImp::showResetOptions()
{
    QMenu menu(this);

    auto currentPageItem = getCurrentPage();
    auto currentGroupItem = static_cast<PreferencesPageItem*>(currentPageItem->parent());

    auto pageText = currentPageItem->text();
    auto groupText = currentGroupItem->text();

    // Reset per page
    QAction* pageAction = menu.addAction(tr("Reset page '%1'...").arg(pageText),
                                         this,
                                         [&] { restorePageDefaults(currentPageItem); });
    pageAction->setToolTip(tr("Resets the user settings for the page '%1'").arg(pageText));

    // Reset per group
    QAction* groupAction = menu.addAction(tr("Reset group '%1'...").arg(groupText),
                                          this,
                                          [&] { restorePageDefaults(static_cast<PreferencesPageItem*>(currentPageItem->parent())); });
    groupAction->setToolTip(tr("Resets the user settings for the group '%1'").arg(groupText));

    // Reset all
    QAction* allAction = menu.addAction(tr("Reset all..."),
                                        this,
                                        &DlgPreferencesImp::restoreDefaults);
    allAction->setToolTip(tr("Resets the user settings entirely"));

    connect(&menu, &QMenu::hovered, [&menu](QAction* hover) {
        QPoint pos = menu.pos();
        pos.rx() += menu.width() + 10;
        QToolTip::showText(pos, hover->toolTip());
    });

    menu.exec(QCursor::pos());
}

void DlgPreferencesImp::restoreDefaults()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Clear user settings"));
    box.setText(tr("Do you want to clear all your user settings?"));
    box.setInformativeText(tr("If you agree all your settings will be cleared."));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);

    if (box.exec() == QMessageBox::Yes) {
        // keep this parameter
        bool saveParameter = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")->
                              GetBool("SaveUserParameter", true);

        ParameterManager* mgr = App::GetApplication().GetParameterSet("User parameter");
        mgr->Clear();

        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")->
                              SetBool("SaveUserParameter", saveParameter);

        reject();
    }
}
/**
 * If the dialog is currently showing and the static variable _pages changed, this function 
 * will rescan that list of pages and add any that are new to the current dialog. It will not
 * remove any pages that are no longer in the list, and will not change the user's current
 * active page.
 */
void DlgPreferencesImp::reloadPages()
{
    // Make sure that pages are ready to create
    GetWidgetFactorySupplier();

    for (const auto &[ group, pages ] : _pages) {
        QString groupName = QString::fromStdString(group);

        // First, does this group already exist?
        PreferencesPageItem* groupItem = nullptr;

        auto root = _model.invisibleRootItem();
        for (int i = 0; i < root->rowCount(); i++) {
            auto currentGroupItem = static_cast<PreferencesPageItem*>(root->child(i));
            auto currentGroupName = currentGroupItem->data(GroupNameRole).toString();

            if (currentGroupName == groupName) {
                groupItem = currentGroupItem;
                break;
            }
        }

        // This is a new group that wasn't there when we started this instance of the dialog: 
        if (!groupItem) {
            groupItem = createGroup(group);
        }

        // Move on to the pages in the group to see if we need to add any
        for (const auto& page : pages) {
            // Does this page already exist?
            QString pageName = QString::fromStdString(page);

            bool pageExists = false;

            for (int i = 0; i < groupItem->rowCount(); i++) {
                auto currentPageItem = static_cast<PreferencesPageItem*>(groupItem->child(i));

                if (currentPageItem->data(PageNameRole).toString() == pageName) {
                    pageExists = true;
                    break;
                }
            }

            // This is a new page that wasn't there when we started this instance of the dialog:
            if (!pageExists) {
                createPageInGroup(groupItem, page);
            }
        }
    }
}

void DlgPreferencesImp::applyChanges()
{
    // Checks if any of the classes that represent several pages of settings
    // (DlgSettings*.*) implement checkSettings() method.  If any of them do,
    // call it to validate if user input is correct.  If something fails (i.e.,
    // not correct), shows a messageBox and set this->invalidParameter = true to
    // cancel further operation in other methods (like in accept()).

    for (int i = 0; i < ui->groupWidgetStack->count(); i++) {
        auto pagesStackWidget = qobject_cast<QStackedWidget*>(ui->groupWidgetStack->widget(i));

        for (int j = 0; j < pagesStackWidget->count(); j++) {
            QWidget* page = pagesStackWidget->widget(j);

            int index = page->metaObject()->indexOfMethod("checkSettings()");

            if (index >= 0) {
                try {
                    page->qt_metacall(QMetaObject::InvokeMetaMethod, index, nullptr);
                }
                catch (const Base::Exception& e) {
                    ui->groupWidgetStack->setCurrentIndex(i);
                    pagesStackWidget->setCurrentIndex(j);

                    QMessageBox::warning(this,
                                            tr("Wrong parameter"),
                                            QString::fromLatin1(e.what()));

                    this->invalidParameter = true;

                    // exit early due to found errors
                    return;
                }
            }
        }
    }

    // If everything is ok (i.e., no validation problem), call method
    // saveSettings() in every subpage (DlgSetting*) object.
    for (int i = 0; i < ui->groupWidgetStack->count(); i++) {
        auto pageStackWidget = qobject_cast<QStackedWidget*>(ui->groupWidgetStack->widget(i));

        for (int j = 0; j < pageStackWidget->count(); j++) {
            auto page = qobject_cast<PreferencePage*>(pageStackWidget->widget(j));
            
            if (page) {
                page->saveSettings();
                restartRequired = restartRequired || page->isRestartRequired();
            }
        }
    }

    bool saveParameter = App::GetApplication()
                             .GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")
                             ->GetBool("SaveUserParameter", true);
    
    if (saveParameter) {
        ParameterManager* parmgr = App::GetApplication().GetParameterSet("User parameter");
        parmgr->SaveDocument(App::Application::Config()["UserParameter"].c_str());
    }
}

void DlgPreferencesImp::restartIfRequired()
{
    if (restartRequired) {
        QMessageBox restartBox;

        restartBox.setIcon(QMessageBox::Warning);
        restartBox.setWindowTitle(tr("Restart required"));
        restartBox.setText(tr("You must restart FreeCAD for changes to take effect."));
        restartBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        restartBox.setDefaultButton(QMessageBox::Cancel);
        auto okBtn = restartBox.button(QMessageBox::Ok);
        auto cancelBtn = restartBox.button(QMessageBox::Cancel);
        okBtn->setText(tr("Restart now"));
        cancelBtn->setText(tr("Restart later"));

        int exec = restartBox.exec();

        if (exec == QMessageBox::Ok) {
            //restart FreeCAD after a delay to give time to this dialog to close
            QTimer::singleShot(1000, []() 
            {
                QStringList args = QApplication::arguments();
                args.pop_front();
                if (getMainWindow()->close())
                    QProcess::startDetached(QApplication::applicationFilePath(), args);
            });
        }
    }
}

void DlgPreferencesImp::showEvent(QShowEvent* ev)
{
    this->adjustSize();
    QDialog::showEvent(ev);
}

QModelIndex findRootIndex(const QModelIndex& index)
{
    auto root = index;

    while (root.parent().isValid()) {
        root = root.parent();
    }
    
    return root;
}

void DlgPreferencesImp::onPageSelected(const QModelIndex& index)
{
    auto root = findRootIndex(index);

    auto* groupItem = static_cast<PreferencesPageItem*>(_model.itemFromIndex(root));
    auto* pagesStackWidget = static_cast<QStackedWidget*>(groupItem->getWidget());

    ui->groupWidgetStack->setCurrentWidget(groupItem->getWidget());

    if (index != root) {
        pagesStackWidget->setCurrentIndex(index.row());
    }

    updatePageDependentLabels();
}

void DlgPreferencesImp::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);

        auto root = _model.invisibleRootItem();
        for (int i = 0; i < root->rowCount(); i++) {
            auto groupItem = static_cast<PreferencesPageItem*>(root->child(i));
            auto groupName = groupItem->data(GroupNameRole).toString();

            groupItem->setText(QObject::tr(groupName.toLatin1()));

            for (int j = 0; j < groupItem->rowCount(); j++) {
                auto pageModelItem = static_cast<PreferencesPageItem*>(groupItem->child(j));
                auto pageModelWidget = static_cast<PreferencePage*>(pageModelItem->getWidget());

                pageModelItem->setText(pageModelWidget->windowTitle());
            }
        }

        updatePageDependentLabels();
    } else {
        QWidget::changeEvent(e);
    }
}

void DlgPreferencesImp::reload()
{
    for (int i = 0; i < ui->groupWidgetStack->count(); i++) {
        auto pageStackWidget = static_cast<QStackedWidget*>(ui->groupWidgetStack->widget(i));

        for (int j = 0; j < pageStackWidget->count(); j++) {
            auto page = qobject_cast<PreferencePage*>(pageStackWidget->widget(j));

            if (page) {
                page->loadSettings();
            }
        }
    }

    applyChanges();
}

void DlgPreferencesImp::restorePageDefaults(PreferencesPageItem* item)
{
    if (item->hasChildren()) {
        // If page has children iterate over them and restore each
        for (int i = 0; i < item->rowCount(); i++) {
            auto child = static_cast<PreferencesPageItem*>(item->child(i));

            restorePageDefaults(child);
        }
    }
    else {
        auto* page = qobject_cast<PreferencePage*>(item->getWidget());
        auto prefs = page->findChildren<QObject*>();

        for (const auto& pref : prefs) {
            if (!pref->property("prefPath").isNull() && !pref->property("prefEntry").isNull()) {
                std::string path = pref->property("prefPath").toString().toStdString();
                std::string entry = pref->property("prefEntry").toString().toStdString();

                ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
                    std::string("User parameter:BaseApp/Preferences/" + path).c_str());

                for (const auto& pn : hGrp->GetParameterNames(entry.c_str())) {
                    hGrp->RemoveAttribute(pn.first, pn.second.c_str());
                }
            }
        }

        std::string pageName = page->property(PageNameProperty).toString().toStdString();
        std::string groupName = page->property(GroupNameProperty).toString().toStdString();

        auto newPage = createPreferencePage(pageName, groupName);

        newPage->loadSettings();

        auto groupPageStack = qobject_cast<QStackedWidget*>(page->parentWidget());
        auto replacedWidgetIndex = groupPageStack->indexOf(page);
        auto currentWidgetIndex = groupPageStack->currentIndex();

        groupPageStack->removeWidget(page);
        groupPageStack->insertWidget(replacedWidgetIndex, newPage);

        item->setWidget(newPage);

        if (replacedWidgetIndex == currentWidgetIndex) {
            groupPageStack->setCurrentIndex(currentWidgetIndex);
        }
    }
}

PreferencesPageItem* DlgPreferencesImp::getCurrentPage() const
{
    auto groupPagesStack = qobject_cast<QStackedWidget*>(ui->groupWidgetStack->currentWidget());

    if (!groupPagesStack) {
        return nullptr;
    }

    auto pageWidget = qobject_cast<PreferencePage*>(groupPagesStack->currentWidget());

    if (!pageWidget) {
        return nullptr;
    }

    return pageWidget->property(PreferencesPageItem::PropertyName).value<PreferencesPageItem*>();
}

#include "moc_DlgPreferencesImp.cpp"
