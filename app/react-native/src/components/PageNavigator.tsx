import React, {useState} from 'react';
import {View, Text, TextInput, StyleSheet, TouchableOpacity} from 'react-native';
import {Colors} from '../theme/colors';

interface PageNavigatorProps {
  currentPage: number;
  totalPages: number;
  viewSize: number;
  connected: boolean;
  onFirst: () => void;
  onPrev: () => void;
  onNext: () => void;
  onLast: () => void;
  onGoToPage: (page: number) => void;
}

export function PageNavigator({
  currentPage,
  totalPages,
  viewSize,
  connected,
  onFirst,
  onPrev,
  onNext,
  onLast,
  onGoToPage,
}: PageNavigatorProps) {
  const [pageInput, setPageInput] = useState(String(currentPage));

  const step = viewSize / 10;
  const hasTotalInfo = totalPages > 0;
  const totalViews = hasTotalInfo ? Math.ceil(totalPages / step) : 0;
  const currentView = Math.floor(currentPage / step) + 1;

  const label = hasTotalInfo
    ? `${currentView} / ${totalViews}`
    : `Page ${currentPage}`;

  const canPrev = connected && currentPage > 0;
  const canNext =
    connected && (!hasTotalInfo || currentPage + step < totalPages);
  const canLast =
    connected && hasTotalInfo && currentPage + step < totalPages;

  return (
    <View style={styles.container}>
      <View style={styles.nav}>
        <NavButton label="«" onPress={onFirst} disabled={!canPrev} />
        <NavButton label="‹" onPress={onPrev} disabled={!canPrev} />
        <Text style={styles.navLabel}>{label}</Text>
        <NavButton label="›" onPress={onNext} disabled={!canNext} />
        <NavButton label="»" onPress={onLast} disabled={!canLast} />
      </View>
      <View style={styles.jump}>
        <TextInput
          style={styles.input}
          value={pageInput}
          onChangeText={setPageInput}
          keyboardType="number-pad"
          returnKeyType="go"
          onSubmitEditing={() => onGoToPage(Number(pageInput) || 0)}
        />
        <TouchableOpacity
          style={styles.goBtn}
          onPress={() => onGoToPage(Number(pageInput) || 0)}
          disabled={!connected}>
          <Text style={[styles.goBtnText, !connected && styles.disabled]}>
            Go
          </Text>
        </TouchableOpacity>
      </View>
    </View>
  );
}

function NavButton({
  label,
  onPress,
  disabled,
}: {
  label: string;
  onPress: () => void;
  disabled: boolean;
}) {
  return (
    <TouchableOpacity
      style={[styles.navBtn, disabled && styles.navBtnDisabled]}
      onPress={onPress}
      disabled={disabled}>
      <Text style={[styles.navBtnText, disabled && styles.disabled]}>
        {label}
      </Text>
    </TouchableOpacity>
  );
}

const styles = StyleSheet.create({
  container: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    flexWrap: 'wrap',
  },
  nav: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: Colors.bg,
    borderWidth: 1,
    borderColor: Colors.border,
    borderRadius: 8,
    overflow: 'hidden',
  },
  navBtn: {
    paddingVertical: 6,
    paddingHorizontal: 10,
    borderRightWidth: 1,
    borderRightColor: Colors.border,
  },
  navBtnDisabled: {
    opacity: 0.35,
  },
  navBtnText: {
    fontSize: 14,
    color: Colors.text,
    textAlign: 'center',
  },
  navLabel: {
    paddingVertical: 6,
    paddingHorizontal: 14,
    fontFamily: Colors.mono,
    fontSize: 12,
    fontWeight: '600',
    color: Colors.text,
    borderRightWidth: 1,
    borderRightColor: Colors.border,
    minWidth: 100,
    textAlign: 'center',
  },
  jump: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
  },
  input: {
    fontFamily: Colors.mono,
    fontSize: 12,
    paddingVertical: 6,
    paddingHorizontal: 8,
    width: 70,
    borderWidth: 1,
    borderColor: Colors.border,
    borderRadius: 8,
    backgroundColor: Colors.bg,
    color: Colors.text,
  },
  goBtn: {
    paddingVertical: 6,
    paddingHorizontal: 10,
    backgroundColor: Colors.surfaceHover,
    borderWidth: 1,
    borderColor: Colors.border,
    borderRadius: 8,
  },
  goBtnText: {
    fontSize: 12,
    fontWeight: '600',
    color: Colors.text,
  },
  disabled: {
    opacity: 0.35,
  },
});
