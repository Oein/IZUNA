import React from 'react';
import {SafeAreaView, ScrollView, StyleSheet, StatusBar} from 'react-native';
import {Colors} from './theme/colors';
import {usePermissions} from './hooks/usePermissions';
import {useBle} from './hooks/useBle';
import {Header} from './components/Header';
import {ConnectionCard} from './components/ConnectionCard';
import {LiveGpsCard} from './components/LiveGpsCard';
import {CommandsCard} from './components/CommandsCard';
import {PageRecordsCard} from './components/PageRecordsCard';
import {ConsoleLogCard} from './components/ConsoleLogCard';

export default function App() {
  const permissionsGranted = usePermissions();
  const {
    bleState,
    deviceName,
    liveGps,
    fileStats,
    downloadProgress,
    records,
    currentPage,
    viewSize,
    lastPageData,
    logMessages,
    connectDevice,
    disconnectDevice,
    getFileSize,
    downloadAll,
    deleteAllData,
    readLog,
    goToPage,
    goFirst,
    goPrev,
    goNext,
    goLast,
    changeViewSize,
    doExportCsv,
    clearLog,
  } = useBle();

  return (
    <SafeAreaView style={styles.safeArea}>
      <StatusBar barStyle="light-content" backgroundColor={Colors.bg} />
      <ScrollView
        style={styles.scroll}
        contentContainerStyle={styles.content}
        showsVerticalScrollIndicator={false}>
        <Header />

        <ConnectionCard
          bleState={bleState}
          deviceName={deviceName}
          onConnect={connectDevice}
          onDisconnect={disconnectDevice}
        />

        <LiveGpsCard liveGps={liveGps} />

        <CommandsCard
          bleState={bleState}
          fileStats={fileStats}
          downloadProgress={downloadProgress}
          onGetSize={getFileSize}
          onDownloadAll={downloadAll}
          onDeleteAll={deleteAllData}
          onReadLog={readLog}
        />

        <PageRecordsCard
          bleState={bleState}
          records={records}
          currentPage={currentPage}
          totalPages={fileStats.totalPages}
          viewSize={viewSize}
          hasData={lastPageData !== null}
          onFirst={goFirst}
          onPrev={goPrev}
          onNext={goNext}
          onLast={goLast}
          onGoToPage={goToPage}
          onDecreaseViewSize={() => changeViewSize(viewSize - 10)}
          onIncreaseViewSize={() => changeViewSize(viewSize + 10)}
          onExportCsv={doExportCsv}
        />

        <ConsoleLogCard messages={logMessages} onClear={clearLog} />
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    backgroundColor: Colors.bg,
  },
  scroll: {
    flex: 1,
  },
  content: {
    paddingHorizontal: 16,
    paddingTop: 24,
    paddingBottom: 48,
    maxWidth: 720,
    alignSelf: 'center',
    width: '100%',
  },
});
