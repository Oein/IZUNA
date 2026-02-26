import React from 'react';
import {View, Text, StyleSheet} from 'react-native';
import {Card} from './Card';
import {Colors} from '../theme/colors';
import type {LiveGps} from '../types';

interface LiveGpsCardProps {
  liveGps: LiveGps;
}

function GpsItem({label, value}: {label: string; value: string}) {
  const isEmpty = value === '\u2014';
  return (
    <View style={styles.item}>
      <Text style={styles.label}>{label}</Text>
      <Text style={[styles.value, isEmpty && styles.valueEmpty]}>{value}</Text>
    </View>
  );
}

export function LiveGpsCard({liveGps}: LiveGpsCardProps) {
  return (
    <Card title="Live GPS — FF01 Notify">
      <View style={styles.grid}>
        <GpsItem label="KST TIME" value={liveGps.kst} />
        <GpsItem label="TIME (COMPACT)" value={liveGps.compactTime} />
        <GpsItem label="LATITUDE" value={liveGps.lat} />
        <GpsItem label="LONGITUDE" value={liveGps.lon} />
      </View>
    </Card>
  );
}

const styles = StyleSheet.create({
  grid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 12,
  },
  item: {
    flex: 1,
    minWidth: '45%',
    backgroundColor: Colors.bg,
    borderWidth: 1,
    borderColor: Colors.border,
    borderRadius: 8,
    padding: 12,
  },
  label: {
    fontSize: 11,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.5,
    color: Colors.textDim,
    marginBottom: 4,
  },
  value: {
    fontFamily: Colors.mono,
    fontSize: 15,
    fontWeight: '600',
    color: Colors.text,
  },
  valueEmpty: {
    color: Colors.textDim,
  },
});
