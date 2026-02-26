import React from 'react';
import {TouchableOpacity, Text, StyleSheet, ViewStyle} from 'react-native';
import {Colors} from '../theme/colors';

interface ThemedButtonProps {
  title: string;
  onPress: () => void;
  variant?: 'default' | 'primary' | 'danger';
  disabled?: boolean;
  small?: boolean;
  style?: ViewStyle;
}

export function ThemedButton({
  title,
  onPress,
  variant = 'default',
  disabled = false,
  small = false,
  style,
}: ThemedButtonProps) {
  const buttonStyles = [
    styles.button,
    small && styles.small,
    variant === 'primary' && styles.primary,
    variant === 'danger' && styles.danger,
    disabled && styles.disabled,
    style,
  ];

  const textStyles = [
    styles.text,
    small && styles.smallText,
    variant === 'primary' && styles.primaryText,
    variant === 'danger' && styles.dangerText,
    disabled && styles.disabledText,
  ];

  return (
    <TouchableOpacity
      style={buttonStyles}
      onPress={onPress}
      disabled={disabled}
      activeOpacity={0.7}>
      <Text style={textStyles}>{title}</Text>
    </TouchableOpacity>
  );
}

const styles = StyleSheet.create({
  button: {
    paddingHorizontal: 16,
    paddingVertical: 8,
    borderRadius: 8,
    borderWidth: 1,
    borderColor: Colors.border,
    backgroundColor: Colors.surfaceHover,
  },
  small: {
    paddingHorizontal: 10,
    paddingVertical: 5,
  },
  primary: {
    backgroundColor: Colors.accent,
    borderColor: Colors.accent,
  },
  danger: {
    borderColor: 'rgba(248, 113, 113, 0.3)',
  },
  disabled: {
    opacity: 0.35,
  },
  text: {
    fontSize: 13,
    fontWeight: '600',
    color: Colors.text,
    textAlign: 'center',
  },
  smallText: {
    fontSize: 11,
  },
  primaryText: {
    color: '#fff',
  },
  dangerText: {
    color: Colors.red,
  },
  disabledText: {},
});
