import React from 'react';
import {View, Text, StyleSheet} from 'react-native';
import {Colors} from '../theme/colors';
import type {DownloadProgress} from '../types';

interface ProgressBarProps {
  progress: DownloadProgress;
}

export function ProgressBar({progress}: ProgressBarProps) {
  if (!progress.visible) {
    return null;
  }

  return (
    <View style={styles.container}>
      <View style={styles.track}>
        <View style={[styles.fill, {width: `${progress.percent}%`}]} />
      </View>
      <Text style={styles.text}>
        {progress.current} / {progress.total} ({progress.percent.toFixed(1)}%)
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    marginTop: 8,
  },
  track: {
    width: '100%',
    height: 6,
    backgroundColor: Colors.bg,
    borderRadius: 3,
    overflow: 'hidden',
    borderWidth: 1,
    borderColor: Colors.border,
  },
  fill: {
    height: '100%',
    backgroundColor: Colors.accent,
    borderRadius: 3,
  },
  text: {
    fontSize: 11,
    fontFamily: Colors.mono,
    color: Colors.textDim,
    marginTop: 6,
    textAlign: 'right',
  },
});
