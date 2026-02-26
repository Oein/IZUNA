import React from 'react';
import {View, Text, StyleSheet, TouchableOpacity} from 'react-native';
import {Colors} from '../theme/colors';

interface ViewSizeStepperProps {
  viewSize: number;
  onDecrease: () => void;
  onIncrease: () => void;
}

export function ViewSizeStepper({
  viewSize,
  onDecrease,
  onIncrease,
}: ViewSizeStepperProps) {
  return (
    <View style={styles.stepper}>
      <TouchableOpacity
        style={styles.btn}
        onPress={onDecrease}
        disabled={viewSize <= 10}>
        <Text
          style={[styles.btnText, viewSize <= 10 && styles.btnTextDisabled]}>
          −
        </Text>
      </TouchableOpacity>
      <Text style={styles.label}>{viewSize}</Text>
      <TouchableOpacity style={[styles.btn, styles.btnLast]} onPress={onIncrease}>
        <Text style={styles.btnText}>+</Text>
      </TouchableOpacity>
    </View>
  );
}

const styles = StyleSheet.create({
  stepper: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: Colors.bg,
    borderWidth: 1,
    borderColor: Colors.border,
    borderRadius: 6,
    overflow: 'hidden',
  },
  btn: {
    paddingVertical: 5,
    paddingHorizontal: 10,
    borderRightWidth: 1,
    borderRightColor: Colors.border,
  },
  btnLast: {
    borderRightWidth: 0,
  },
  btnText: {
    fontSize: 13,
    fontWeight: '700',
    color: Colors.text,
  },
  btnTextDisabled: {
    opacity: 0.35,
  },
  label: {
    paddingVertical: 5,
    paddingHorizontal: 10,
    fontFamily: Colors.mono,
    fontSize: 11,
    fontWeight: '600',
    color: Colors.text,
    borderRightWidth: 1,
    borderRightColor: Colors.border,
    textAlign: 'center',
    minWidth: 50,
  },
});
