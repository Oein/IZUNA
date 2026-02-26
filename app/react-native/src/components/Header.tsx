import React from 'react';
import {View, Text, StyleSheet} from 'react-native';
import {Colors} from '../theme/colors';

export function Header() {
  return (
    <View style={styles.header}>
      <View style={styles.icon}>
        <Text style={styles.iconText}>📡</Text>
      </View>
      <View>
        <Text style={styles.title}>Georecord BLE</Text>
        <Text style={styles.subtitle}>Bluetooth GPS data client</Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
    marginBottom: 28,
  },
  icon: {
    width: 42,
    height: 42,
    backgroundColor: Colors.accentGlow,
    borderWidth: 1,
    borderColor: Colors.accent,
    borderRadius: 12,
    justifyContent: 'center',
    alignItems: 'center',
  },
  iconText: {
    fontSize: 20,
  },
  title: {
    fontSize: 20,
    fontWeight: '700',
    color: Colors.text,
    letterSpacing: -0.3,
  },
  subtitle: {
    fontSize: 13,
    color: Colors.textDim,
  },
});
