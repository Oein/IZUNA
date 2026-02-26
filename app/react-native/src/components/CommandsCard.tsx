import React from 'react';
import {View, Text, StyleSheet} from 'react-native';
import {Card} from './Card';
import {ThemedButton} from './ThemedButton';
import {ProgressBar} from './ProgressBar';
import {Colors} from '../theme/colors';
import type {BleState, FileStats, DownloadProgress} from '../types';

interface CommandsCardProps {
  bleState: BleState;
  fileStats: FileStats;
  downloadProgress: DownloadProgress;
  onGetSize: () => void;
  onDownloadAll: () => void;
  onDeleteAll: () => void;
}

export function CommandsCard({
  bleState,
  fileStats,
  downloadProgress,
  onGetSize,
  onDownloadAll,
  onDeleteAll,
}: CommandsCardProps) {
  const connected = bleState === 'connected';
  const lastPage =
    fileStats.totalPages > 0 ? fileStats.totalPages - 1 : -1;

  return (
    <Card title="Commands — FF02">
      <View style={styles.section}>
        <View style={styles.row}>
          <ThemedButton
            title="Get File Size"
            onPress={onGetSize}
            disabled={!connected}
          />
          <Text style={styles.info}>
            {fileStats.fileSize > 0
              ? `${fileStats.fileSize.toLocaleString()} bytes`
              : '\u2014'}
          </Text>
        </View>
        {fileStats.totalPages > 0 && (
          <View style={styles.row}>
            <Text style={styles.info}>{fileStats.totalPages} pages</Text>
            <Text style={styles.info}>last: {lastPage}</Text>
          </View>
        )}

        <View style={styles.row}>
          <ThemedButton
            title="Download All"
            onPress={onDownloadAll}
            disabled={!connected || downloadProgress.visible}
          />
        </View>
        <ProgressBar progress={downloadProgress} />

        <View style={styles.separator} />

        <View style={styles.row}>
          <ThemedButton
            title="Delete All Data"
            variant="danger"
            onPress={onDeleteAll}
            disabled={!connected}
          />
        </View>
      </View>
    </Card>
  );
}

const styles = StyleSheet.create({
  section: {
    gap: 12,
  },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 10,
    flexWrap: 'wrap',
  },
  info: {
    fontSize: 13,
    fontFamily: Colors.mono,
    color: Colors.textDim,
    backgroundColor: Colors.bg,
    paddingVertical: 5,
    paddingHorizontal: 10,
    borderRadius: 6,
    borderWidth: 1,
    borderColor: Colors.border,
    overflow: 'hidden',
  },
  separator: {
    width: '100%',
    height: 1,
    backgroundColor: Colors.border,
    marginVertical: 4,
  },
});
