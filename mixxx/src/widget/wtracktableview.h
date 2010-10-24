#ifndef WTRACKTABLEVIEW_H
#define WTRACKTABLEVIEW_H

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>

#include "configobject.h"
#include "trackinfoobject.h"
#include "library/libraryview.h"
#include "library/searchthread.h"
#include "library/trackmodel.h" // Can't forward declare enums
#include "widget/wlibrarytableview.h"


class DlgTrackInfo;
class TrackCollection;

const QString WTRACKTABLEVIEW_VSCROLLBARPOS_KEY = "VScrollBarPos"; /** ConfigValue key for QTable vertical scrollbar position */
const QString LIBRARY_CONFIGVALUE = "[Library]"; /** ConfigValue "value" (wtf) for library stuff */


class WTrackTableView : public WLibraryTableView
{
    Q_OBJECT
 	public:
    WTrackTableView(QWidget* parent, ConfigObject<ConfigValue>* pConfig,
                    TrackCollection* pTrackCollection);
    virtual ~WTrackTableView();
    void contextMenuEvent(QContextMenuEvent * event);
    void onSearchStarting();
    void onSearchCleared();
    void onSearch(const QString& text);
    void onShow();
    QWidget* getWidgetForMIDIControl();
    virtual void keyPressEvent(QKeyEvent* event);
public slots:
    void loadTrackModel(QAbstractItemModel* model);
    void slotMouseDoubleClicked(const QModelIndex &);
private slots:
    void slotRemove();
    void slotShowTrackInfo();
    void slotNextTrackInfo();
    void slotPrevTrackInfo();
    void slotSendToAutoDJ();
    void addSelectionToPlaylist(int iPlaylistId);
    void addSelectionToCrate(int iCrateId);
    void loadSelectionToGroup(QString group);

private:
    void showTrackInfo(QModelIndex index);
    void createActions();
    void dragMoveEvent(QDragMoveEvent * event);
    void dragEnterEvent(QDragEnterEvent * event);
    void dropEvent(QDropEvent * event);

    // Returns the current TrackModel, or returns NULL if none is set.
    TrackModel* getTrackModel();
    bool modelHasCapabilities(TrackModel::CapabilitiesFlags capability);

    ConfigObject<ConfigValue> * m_pConfig;
    TrackCollection* m_pTrackCollection;
    //QList<QString> m_selectedTrackLocations;
    QModelIndexList m_selectedIndices;
    QSignalMapper m_loadTrackMapper;

    DlgTrackInfo* pTrackInfo;
    QModelIndex currentTrackInfoIndex;

    SearchThread m_searchThread;

    //Used for right-click operations
    /** Right-click menu */
    QMenu *m_pMenu, *m_pPlaylistMenu, *m_pCrateMenu, *m_pSamplerMenu;;
    QSignalMapper m_playlistMapper, m_crateMapper;
    /**Send to AutoDJ Action**/
    QAction *m_pAutoDJAct;
    /**Send to Player 1 Action**/
    QAction *m_pPlayer1Act;
    /**Send to Player 2 Action**/
    QAction *m_pPlayer2Act;
    /**Send to Samplers Action**/
    QAction *m_pSampler1Act;
    QAction *m_pSampler2Act;
    QAction *m_pSampler3Act;
    QAction *m_pSampler4Act;
    /**Remove from Table Action**/
    QAction *m_pRemoveAct;
    /**Shows track editor/BPM tap**/
    QAction *m_pPropertiesAct;
};

#endif
