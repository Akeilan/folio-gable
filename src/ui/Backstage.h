// Folio Gable — the File view (Backstage), laid out like Folio Writer's and Folio Sheets':
// Home, New, Open, Info, Save, Save As, Import, Print, Export, Close, Options.
#pragma once
#include <QPixmap>
#include <QPushButton>
#include <QWidget>
#include <functional>
#include <map>

class QStackedWidget;
class QVBoxLayout;
class QTreeWidget;

namespace gable {

struct ModelTemplate {
    QString name, note;
};

struct BackstageHost {
    std::function<std::vector<ModelTemplate>()> templates;
    std::function<void(int)> newFromTemplate;
    std::function<void()> openDialog;
    std::function<void(const QString&)> openPath;
    std::function<QStringList()> recent;
    std::function<void(const QString&)> forgetRecent;
    std::function<void()> save, saveAs, importFile, print, exportImage, options, modelInfo, purge, closeModel;
    std::function<void(const QString&)> exportModel;
    std::function<QString()> filePath;
    std::function<std::vector<std::pair<QString, QString>>()> properties;
    std::function<QPixmap(const QString&)> thumbnail;   // of a saved model, may be null
};

class Backstage : public QWidget {
    Q_OBJECT
public:
    Backstage(const BackstageHost& host, QWidget* parent);
    void open(const QString& page = "home");
    void close();
    bool isOpen() const { return isVisible(); }
signals:
    void closed();
protected:
    void keyPressEvent(QKeyEvent* e) override;
private:
    BackstageHost host_;
    QStackedWidget* stack_;
    std::map<QString, QPushButton*> nav_;
    QString current_;
    void nav(const QString& key);
    QWidget* page(const QString& key);
    QWidget* pageHome();
    QWidget* pageNew();
    QWidget* pageOpen();
    QWidget* pageInfo();
    QWidget* pageSaveAs();
    QWidget* pagePrint();
    QWidget* pageExport();
    QWidget* frame(const QString& title, QVBoxLayout*& lay);
    QWidget* templateTile(int i, const ModelTemplate& t);
    QTreeWidget* recentList(int limit, const QString& filter);
    QPushButton* bigButton(const QString& icon, const QString& title, const QString& desc, std::function<void()> fn);
    void run(std::function<void()> fn);
};

}  // namespace gable
