import {useEffect, useState} from 'react';
import {Platform, PermissionsAndroid} from 'react-native';

export function usePermissions() {
  const [granted, setGranted] = useState(false);

  useEffect(() => {
    requestPermissions().then(setGranted);
  }, []);

  return granted;
}

async function requestPermissions(): Promise<boolean> {
  if (Platform.OS === 'ios') {
    // iOS permissions are handled via Info.plist entries.
    // BLE permissions are requested automatically by react-native-ble-plx.
    return true;
  }

  if (Platform.OS === 'android') {
    const apiLevel = Platform.Version;

    if (apiLevel >= 31) {
      // Android 12+
      const results = await PermissionsAndroid.requestMultiple([
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
        PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
      ]);
      return Object.values(results).every(
        r => r === PermissionsAndroid.RESULTS.GRANTED,
      );
    } else {
      // Android < 12
      const result = await PermissionsAndroid.request(
        PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
      );
      return result === PermissionsAndroid.RESULTS.GRANTED;
    }
  }

  return false;
}
