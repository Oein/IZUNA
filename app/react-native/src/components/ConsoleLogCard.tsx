import React, {useRef, useEffect} from 'react';
import {View, Text, ScrollView, StyleSheet} from 'react-native';
import {Card} from './Card';
import {ThemedButton} from './ThemedButton';
import {Colors} from '../theme/colors';

interface ConsoleLogCardProps {
  messages: string[];
  onClear: () => void;
}

export function ConsoleLogCard({messages, onClear}: ConsoleLogCardProps) {
  const scrollRef = useRef<ScrollView>(null);

  useEffect(() => {
    scrollRef.current?.scrollToEnd({animated: false});
  }, [messages]);

  return (
    <Card>
      <View style={styles.wrapper}>
        <View style={styles.header}>
          <Text style={styles.headerText}>CONSOLE LOG</Text>
          <ThemedButton title="Clear" small onPress={onClear} />
        </View>
        <ScrollView
          ref={scrollRef}
          style={styles.logArea}
          showsVerticalScrollIndicator>
          <Text style={styles.logText} selectable>
            {messages.join('\n')}
          </Text>
        </ScrollView>
      </View>
    </Card>
  );
}

const styles = StyleSheet.create({
  wrapper: {
    backgroundColor: Colors.bg,
    borderWidth: 1,
    borderColor: Colors.border,
    borderRadius: 8,
    overflow: 'hidden',
  },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingVertical: 8,
    paddingHorizontal: 12,
    borderBottomWidth: 1,
    borderBottomColor: Colors.border,
    backgroundColor: 'rgba(255,255,255,0.02)',
  },
  headerText: {
    fontSize: 11,
    color: Colors.textDim,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.5,
  },
  logArea: {
    height: 200,
    padding: 10,
  },
  logText: {
    fontFamily: Colors.mono,
    fontSize: 12,
    lineHeight: 19,
    color: Colors.textDim,
  },
});
