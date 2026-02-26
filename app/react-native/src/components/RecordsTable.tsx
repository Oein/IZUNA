import React from 'react';
import {View, Text, FlatList, StyleSheet} from 'react-native';
import {Colors} from '../theme/colors';
import {formatKstFromCompact} from '../utils/formatting';
import type {GpsRecord} from '../types';

interface RecordsTableProps {
  records: GpsRecord[];
}

export function RecordsTable({records}: RecordsTableProps) {
  return (
    <View style={styles.wrapper}>
      {/* Header */}
      <View style={styles.headerRow}>
        <Text style={[styles.headerCell, styles.indexCol]}>#</Text>
        <Text style={[styles.headerCell, styles.timeCol]}>KST TIME</Text>
        <Text style={[styles.headerCell, styles.coordCol]}>LATITUDE</Text>
        <Text style={[styles.headerCell, styles.coordCol]}>LONGITUDE</Text>
      </View>

      {/* Body */}
      <FlatList
        data={records}
        keyExtractor={(_, index) => String(index)}
        renderItem={({item, index}) => {
          const isEmpty =
            item.compactTime === 0n && item.lat === 0 && item.lon === 0;
          return (
            <View style={styles.row}>
              <Text
                style={[
                  styles.cell,
                  styles.indexCol,
                  isEmpty && styles.emptyCell,
                ]}>
                {index}
              </Text>
              <Text
                style={[
                  styles.cell,
                  styles.timeCol,
                  isEmpty && styles.emptyCell,
                ]}>
                {isEmpty ? '\u2014' : formatKstFromCompact(item.compactTime)}
              </Text>
              <Text
                style={[
                  styles.cell,
                  styles.coordCol,
                  isEmpty && styles.emptyCell,
                ]}>
                {isEmpty ? '\u2014' : item.lat.toFixed(6)}
              </Text>
              <Text
                style={[
                  styles.cell,
                  styles.coordCol,
                  isEmpty && styles.emptyCell,
                ]}>
                {isEmpty ? '\u2014' : item.lon.toFixed(6)}
              </Text>
            </View>
          );
        }}
        scrollEnabled={false}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  wrapper: {
    borderWidth: 1,
    borderColor: Colors.border,
    borderRadius: 8,
    overflow: 'hidden',
  },
  headerRow: {
    flexDirection: 'row',
    backgroundColor: 'rgba(255,255,255,0.03)',
    borderBottomWidth: 1,
    borderBottomColor: Colors.border,
  },
  headerCell: {
    fontSize: 11,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.5,
    color: Colors.textDim,
    paddingVertical: 10,
    paddingHorizontal: 12,
  },
  row: {
    flexDirection: 'row',
    borderBottomWidth: 1,
    borderBottomColor: Colors.border,
  },
  cell: {
    fontFamily: Colors.mono,
    fontSize: 12,
    color: Colors.text,
    paddingVertical: 8,
    paddingHorizontal: 12,
  },
  emptyCell: {
    color: Colors.textDim,
    opacity: 0.5,
  },
  indexCol: {
    width: 40,
  },
  timeCol: {
    flex: 2,
  },
  coordCol: {
    flex: 1.5,
  },
});
