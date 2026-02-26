import React from 'react';
import {View, Text, StyleSheet} from 'react-native';
import {Card} from './Card';
import {ThemedButton} from './ThemedButton';
import {StatusBadge} from './StatusBadge';
import {Colors} from '../theme/colors';
import type {BleState} from '../types';

interface ConnectionCardProps {
  bleState: BleState;
  deviceName: string;
  onConnect: () => void;
  onDisconnect: () => void;
}

export function ConnectionCard({
  bleState,
  deviceName,
  onConnect,
  onDisconnect,
}: ConnectionCardProps) {
  const connected = bleState === 'connected';
  const busy = bleState === 'scanning' || bleState === 'connecting';

  return (
    <Card>
      <View style={styles.bar}>
        <ThemedButton
          title="Connect"
          variant="primary"
          onPress={onConnect}
          disabled={connected || busy}
        />
        <ThemedButton
          title="Disconnect"
          onPress={onDisconnect}
          disabled={!connected}
        />
        <StatusBadge state={bleState} />
      </View>
      {deviceName ? (
        <Text style={styles.deviceName}>{deviceName}</Text>
      ) : null}
    </Card>
  );
}

const styles = StyleSheet.create({
  bar: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    flexWrap: 'wrap',
  },
  deviceName: {
    marginTop: 8,
    fontSize: 13,
    fontFamily: Colors.mono,
    color: Colors.textDim,
  },
});
