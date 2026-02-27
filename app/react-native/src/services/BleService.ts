import {BleManager, Device, Subscription} from 'react-native-ble-plx';
import {base64ToUint8Array, uint8ArrayToBase64} from '../utils/base64';
import {parseUint32LE} from './BinaryParser';

// 16-bit UUID 0x00FF → full 128-bit
const SERVICE_UUID = '000000ff-0000-1000-8000-00805f9b34fb';
const DATA_CHAR_UUID = '0000ff01-0000-1000-8000-00805f9b34fb';
const CMD_CHAR_UUID = '0000ff02-0000-1000-8000-00805f9b34fb';

const GATT_COOLDOWN_MS = 80;
const SCAN_TIMEOUT_MS = 15000;

type CommandResult =
  | {type: 'size'; size: number}
  | {type: 'page'; pageData: Uint8Array}
  | {type: 'delete'; success: boolean}
  | {type: 'log'; logData: Uint8Array};

interface PendingCommand {
  expectedType: number;
  resolve: (value: CommandResult) => void;
  reject: (error: Error) => void;
  timeoutId: ReturnType<typeof setTimeout>;
}

export type OnLiveGpsCallback = (data: Uint8Array) => void;
export type OnDisconnectCallback = () => void;
export type OnLogCallback = (message: string) => void;

class BleService {
  private manager: BleManager;
  private device: Device | null = null;
  private pendingCommand: PendingCommand | null = null;
  private gattLock: Promise<void> = Promise.resolve();
  private dataSubscription: Subscription | null = null;
  private cmdSubscription: Subscription | null = null;
  private disconnectSubscription: Subscription | null = null;

  onLiveGps: OnLiveGpsCallback | null = null;
  onDisconnect: OnDisconnectCallback | null = null;
  onLog: OnLogCallback | null = null;

  constructor() {
    this.manager = new BleManager();
  }

  private log(msg: string) {
    this.onLog?.(msg);
  }

  private delay(ms: number): Promise<void> {
    return new Promise(r => setTimeout(r, ms));
  }

  get isConnected(): boolean {
    return this.device !== null;
  }

  get deviceName(): string {
    return this.device?.name || this.device?.localName || '(unknown)';
  }

  async scan(): Promise<Device> {
    return new Promise<Device>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.manager.stopDeviceScan();
        reject(new Error('Scan timeout (15s)'));
      }, SCAN_TIMEOUT_MS);

      this.manager.startDeviceScan(
        [SERVICE_UUID],
        {allowDuplicates: false},
        (error, scannedDevice) => {
          if (error) {
            clearTimeout(timeout);
            this.manager.stopDeviceScan();
            reject(error);
            return;
          }
          if (scannedDevice) {
            clearTimeout(timeout);
            this.manager.stopDeviceScan();
            resolve(scannedDevice);
          }
        },
      );
    });
  }

  async connect(): Promise<string> {
    this.log('Scanning for device...');
    const scannedDevice = await this.scan();
    this.log(`Found: ${scannedDevice.name || scannedDevice.localName || scannedDevice.id}`);

    const connected = await scannedDevice.connect({requestMTU: 256});
    await connected.discoverAllServicesAndCharacteristics();
    this.device = connected;

    // Monitor disconnect
    this.disconnectSubscription = this.manager.onDeviceDisconnected(
      connected.id,
      () => {
        this.cleanup();
        this.onDisconnect?.();
      },
    );

    // Monitor DATA characteristic (FF01) for live GPS
    this.dataSubscription = connected.monitorCharacteristicForService(
      SERVICE_UUID,
      DATA_CHAR_UUID,
      (error, characteristic) => {
        if (error) {
          this.log(`DATA notify error: ${error.message}`);
          return;
        }
        if (characteristic?.value) {
          const data = base64ToUint8Array(characteristic.value);
          this.onLiveGps?.(data);
        }
      },
    );

    // Monitor CMD characteristic (FF02) for command responses
    this.cmdSubscription = connected.monitorCharacteristicForService(
      SERVICE_UUID,
      CMD_CHAR_UUID,
      (error, characteristic) => {
        if (error) {
          this.log(`CMD notify error: ${error.message}`);
          return;
        }
        if (characteristic?.value) {
          const bytes = base64ToUint8Array(characteristic.value);
          this.handleCmdResponse(bytes);
        }
      },
    );

    const name = connected.name || connected.localName || '(unknown)';
    this.log(`Connected: ${name}`);
    return name;
  }

  private handleCmdResponse(bytes: Uint8Array) {
    if (!this.pendingCommand) {
      this.log(`FF02 notify (unmatched): len=${bytes.length}`);
      return;
    }

    const {expectedType, resolve, reject, timeoutId} = this.pendingCommand;
    clearTimeout(timeoutId);
    this.pendingCommand = null;

    if (expectedType === 0x00 && bytes.length === 4) {
      const size = parseUint32LE(bytes, 0);
      resolve({type: 'size', size});
      return;
    }

    if (expectedType === 0x01 && bytes.length === 240) {
      resolve({type: 'page', pageData: new Uint8Array(bytes)});
      return;
    }

    if (expectedType === 0x02 && bytes.length === 1) {
      resolve({type: 'delete', success: bytes[0] === 0x01});
      return;
    }

    if (expectedType === 0x03 && bytes.length === 240) {
      resolve({type: 'log', logData: new Uint8Array(bytes)});
      return;
    }

    reject(
      new Error(
        `Unexpected response: cmd=0x${expectedType.toString(16)}, len=${bytes.length}`,
      ),
    );
  }

  sendCommand(
    payload: Uint8Array,
    expectedType: number,
    timeoutMs: number = 3000,
    retries: number = 2,
  ): Promise<CommandResult> {
    const op = this.gattLock.then(() =>
      this._sendCommandOnce(payload, expectedType, timeoutMs, retries),
    );
    this.gattLock = op
      .catch(() => {})
      .then(() => this.delay(GATT_COOLDOWN_MS));
    return op;
  }

  private async _sendCommandOnce(
    payload: Uint8Array,
    expectedType: number,
    timeoutMs: number,
    retries: number,
  ): Promise<CommandResult> {
    if (!this.device) {
      throw new Error('Device not connected');
    }

    for (let attempt = 0; attempt <= retries; attempt++) {
      try {
        return await new Promise<CommandResult>(async (resolve, reject) => {
          const timeoutId = setTimeout(() => {
            if (this.pendingCommand) {
              this.pendingCommand = null;
              reject(new Error('command timeout'));
            }
          }, timeoutMs);

          this.pendingCommand = {expectedType, resolve, reject, timeoutId};

          try {
            const b64 = uint8ArrayToBase64(payload);
            await this.device!.writeCharacteristicWithResponseForService(
              SERVICE_UUID,
              CMD_CHAR_UUID,
              b64,
            );
          } catch (err: any) {
            clearTimeout(timeoutId);
            this.pendingCommand = null;
            reject(err);
          }
        });
      } catch (err: any) {
        const isGattBusy =
          err.message && err.message.includes('GATT');
        if (isGattBusy && attempt < retries) {
          this.log(`GATT busy, retry ${attempt + 1}/${retries}...`);
          await this.delay(150 * (attempt + 1));
          continue;
        }
        throw err;
      }
    }

    throw new Error('Exhausted retries');
  }

  async disconnect() {
    try {
      if (this.device) {
        await this.device.cancelConnection();
      }
    } catch {
      // ignore
    }
    this.cleanup();
  }

  private cleanup() {
    this.dataSubscription?.remove();
    this.cmdSubscription?.remove();
    this.disconnectSubscription?.remove();
    this.dataSubscription = null;
    this.cmdSubscription = null;
    this.disconnectSubscription = null;

    if (this.pendingCommand) {
      clearTimeout(this.pendingCommand.timeoutId);
      this.pendingCommand = null;
    }

    this.device = null;
    this.gattLock = Promise.resolve();
  }

  destroy() {
    this.cleanup();
    this.manager.destroy();
  }
}

// Singleton
export const bleService = new BleService();
