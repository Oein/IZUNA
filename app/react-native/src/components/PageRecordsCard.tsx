import React from 'react';
import {View, Text, StyleSheet} from 'react-native';
import {Card} from './Card';
import {PageNavigator} from './PageNavigator';
import {ViewSizeStepper} from './ViewSizeStepper';
import {RecordsTable} from './RecordsTable';
import {GpsMap} from './GpsMap';
import {ThemedButton} from './ThemedButton';
import {Colors} from '../theme/colors';
import type {GpsRecord, BleState} from '../types';

interface PageRecordsCardProps {
  bleState: BleState;
  records: GpsRecord[];
  currentPage: number;
  totalPages: number;
  viewSize: number;
  hasData: boolean;
  onFirst: () => void;
  onPrev: () => void;
  onNext: () => void;
  onLast: () => void;
  onGoToPage: (page: number) => void;
  onDecreaseViewSize: () => void;
  onIncreaseViewSize: () => void;
  onExportCsv: () => void;
}

export function PageRecordsCard({
  bleState,
  records,
  currentPage,
  totalPages,
  viewSize,
  hasData,
  onFirst,
  onPrev,
  onNext,
  onLast,
  onGoToPage,
  onDecreaseViewSize,
  onIncreaseViewSize,
  onExportCsv,
}: PageRecordsCardProps) {
  const connected = bleState === 'connected';
  const validCount = records.filter(
    r => !(r.compactTime === 0n && r.lat === 0 && r.lon === 0),
  ).length;

  return (
    <Card title="Page Records">
      {!hasData ? (
        <View style={styles.emptyState}>
          <Text style={styles.emptyIcon}>📋</Text>
          <Text style={styles.emptyTitle}>No page loaded</Text>
          <Text style={styles.emptySubtitle}>
            Connect to a device, then use Download All
          </Text>
        </View>
      ) : (
        <>
          {/* Toolbar */}
          <PageNavigator
            currentPage={currentPage}
            totalPages={totalPages}
            viewSize={viewSize}
            connected={connected}
            onFirst={onFirst}
            onPrev={onPrev}
            onNext={onNext}
            onLast={onLast}
            onGoToPage={onGoToPage}
          />

          <View style={styles.toolbarRow}>
            <View style={styles.badge}>
              <Text style={styles.badgeText}>
                {validCount} / {records.length} records
              </Text>
            </View>
            <ViewSizeStepper
              viewSize={viewSize}
              onDecrease={onDecreaseViewSize}
              onIncrease={onIncreaseViewSize}
            />
            <ThemedButton title="CSV" small onPress={onExportCsv} />
          </View>

          {/* Map */}
          <GpsMap records={records} />

          {/* Table */}
          <RecordsTable records={records} />
        </>
      )}
    </Card>
  );
}

const styles = StyleSheet.create({
  toolbarRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    marginVertical: 12,
    flexWrap: 'wrap',
  },
  badge: {
    paddingVertical: 4,
    paddingHorizontal: 10,
    borderRadius: 6,
    backgroundColor: Colors.accentGlow,
    borderWidth: 1,
    borderColor: 'rgba(108, 140, 255, 0.2)',
  },
  badgeText: {
    fontSize: 11,
    fontWeight: '600',
    fontFamily: Colors.mono,
    color: Colors.accent,
  },
  emptyState: {
    alignItems: 'center',
    justifyContent: 'center',
    paddingVertical: 40,
    paddingHorizontal: 20,
  },
  emptyIcon: {
    fontSize: 28,
    marginBottom: 10,
    opacity: 0.5,
  },
  emptyTitle: {
    fontSize: 13,
    fontWeight: '600',
    color: Colors.text,
    marginBottom: 2,
  },
  emptySubtitle: {
    fontSize: 13,
    color: Colors.textDim,
    textAlign: 'center',
  },
});
