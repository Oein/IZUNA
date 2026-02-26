import React, {useMemo} from 'react';
import {View, StyleSheet} from 'react-native';
import MapView, {Polyline, Marker, PROVIDER_DEFAULT} from 'react-native-maps';
import {Colors} from '../theme/colors';
import {formatKstFromCompact} from '../utils/formatting';
import type {GpsRecord} from '../types';

interface GpsMapProps {
  records: GpsRecord[];
}

export function GpsMap({records}: GpsMapProps) {
  const validCoords = useMemo(() => {
    return records.filter(
      r => !(r.compactTime === 0n && r.lat === 0 && r.lon === 0),
    );
  }, [records]);

  if (validCoords.length === 0) {
    return null;
  }

  const latLngs = validCoords.map(c => ({
    latitude: c.lat,
    longitude: c.lon,
  }));

  // Calculate region to fit all points
  const lats = validCoords.map(c => c.lat);
  const lons = validCoords.map(c => c.lon);
  const minLat = Math.min(...lats);
  const maxLat = Math.max(...lats);
  const minLon = Math.min(...lons);
  const maxLon = Math.max(...lons);
  const latDelta = Math.max((maxLat - minLat) * 1.3, 0.005);
  const lonDelta = Math.max((maxLon - minLon) * 1.3, 0.005);

  const region = {
    latitude: (minLat + maxLat) / 2,
    longitude: (minLon + maxLon) / 2,
    latitudeDelta: latDelta,
    longitudeDelta: lonDelta,
  };

  const first = validCoords[0];
  const last = validCoords[validCoords.length - 1];

  return (
    <View style={styles.container}>
      <MapView
        provider={PROVIDER_DEFAULT}
        style={styles.map}
        initialRegion={region}>
        <Polyline
          coordinates={latLngs}
          strokeColor={Colors.accent}
          strokeWidth={3}
        />
        <Marker
          coordinate={latLngs[0]}
          title="Start"
          description={formatKstFromCompact(first.compactTime)}
          pinColor={Colors.green}
        />
        {validCoords.length > 1 && (
          <Marker
            coordinate={latLngs[latLngs.length - 1]}
            title="End"
            description={formatKstFromCompact(last.compactTime)}
            pinColor={Colors.red}
          />
        )}
      </MapView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    borderWidth: 1,
    borderColor: Colors.border,
    borderRadius: 8,
    overflow: 'hidden',
    marginBottom: 12,
    height: 300,
  },
  map: {
    ...StyleSheet.absoluteFillObject,
  },
});
