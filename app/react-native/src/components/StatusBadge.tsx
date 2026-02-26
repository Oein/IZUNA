import React, {useEffect, useRef} from 'react';
import {View, Text, StyleSheet, Animated} from 'react-native';
import {Colors} from '../theme/colors';
import type {BleState} from '../types';

interface StatusBadgeProps {
  state: BleState;
}

export function StatusBadge({state}: StatusBadgeProps) {
  const pulseAnim = useRef(new Animated.Value(1)).current;
  const connected = state === 'connected';

  useEffect(() => {
    if (connected) {
      const animation = Animated.loop(
        Animated.sequence([
          Animated.timing(pulseAnim, {
            toValue: 0.4,
            duration: 1000,
            useNativeDriver: true,
          }),
          Animated.timing(pulseAnim, {
            toValue: 1,
            duration: 1000,
            useNativeDriver: true,
          }),
        ]),
      );
      animation.start();
      return () => animation.stop();
    } else {
      pulseAnim.setValue(1);
    }
  }, [connected, pulseAnim]);

  const label =
    state === 'connected'
      ? 'Connected'
      : state === 'scanning'
        ? 'Scanning...'
        : state === 'connecting'
          ? 'Connecting...'
          : 'Disconnected';

  return (
    <View style={[styles.badge, connected && styles.badgeConnected]}>
      <Animated.View
        style={[
          styles.dot,
          connected && styles.dotConnected,
          {opacity: pulseAnim},
        ]}
      />
      <Text style={[styles.label, connected && styles.labelConnected]}>
        {label}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  badge: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    paddingVertical: 5,
    paddingHorizontal: 12,
    borderRadius: 20,
    backgroundColor: Colors.redGlow,
    borderWidth: 1,
    borderColor: 'rgba(248, 113, 113, 0.25)',
  },
  badgeConnected: {
    backgroundColor: Colors.greenGlow,
    borderColor: 'rgba(74, 222, 128, 0.25)',
  },
  dot: {
    width: 7,
    height: 7,
    borderRadius: 3.5,
    backgroundColor: Colors.red,
  },
  dotConnected: {
    backgroundColor: Colors.green,
  },
  label: {
    fontSize: 12,
    fontWeight: '600',
    fontFamily: Colors.mono,
    color: Colors.red,
  },
  labelConnected: {
    color: Colors.green,
  },
});
