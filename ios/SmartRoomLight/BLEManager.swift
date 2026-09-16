// Handles the BLE connection to the ESP32-Light peripheral. Used both by
// the app's own UI and by the App Intents in LightIntents.swift (each
// intent's perform() calls BLEManager.shared.sendCommand(_:)).
//
// Pairing: the first time this connects, iOS will show a "Bluetooth
// Pairing Request" prompt asking for a 6-digit PIN -- enter the
// STATIC_PASSKEY value from the ESP32 firmware. After that, the bond is
// cached by iOS and no further prompts appear.
//
// Uses CoreBluetooth state restoration (CBCentralManagerOptionRestoreIdentifierKey)
// so iOS can relaunch this app in the background when the ESP32 comes
// back into range, keeping the connection warm without you having to
// open the app first.

import CoreBluetooth
import Foundation

final class BLEManager: NSObject, ObservableObject {
    static let shared = BLEManager()

    private let serviceUUID = CBUUID(string: "a1b2c3d4-0001-1000-8000-00805f9b34fb")
    private let statusCharUUID = CBUUID(string: "a1b2c3d4-0002-1000-8000-00805f9b34fb")
    private let controlCharUUID = CBUUID(string: "a1b2c3d4-0003-1000-8000-00805f9b34fb")

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var controlChar: CBCharacteristic?

    @Published var isConnected = false
    @Published var lastStatus = ""

    private var connectContinuation: CheckedContinuation<Void, Error>?
    private var writeContinuation: CheckedContinuation<Void, Error>?
    private var connectTimeoutTask: Task<Void, Never>?

    enum BLEError: Error {
        case bluetoothUnavailable
        case notReady
        case timedOut
    }

    private override init() {
        super.init()
        centralManager = CBCentralManager(
            delegate: self,
            queue: nil,
            options: [CBCentralManagerOptionRestoreIdentifierKey: "ESP32LightCentral"]
        )
    }

    /// Sends one command index (see the ESP32 firmware's COMMANDS table)
    /// over the CONTROL characteristic, connecting first if needed.
    func sendCommand(_ index: UInt8) async throws {
        try await ensureConnected()
        guard let peripheral, let controlChar else {
            throw BLEError.notReady
        }
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            self.writeContinuation = continuation
            peripheral.writeValue(Data([index]), for: controlChar, type: .withResponse)
        }
    }

    private func ensureConnected() async throws {
        if isConnected, peripheral != nil, controlChar != nil { return }

        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            self.connectContinuation = continuation

            connectTimeoutTask?.cancel()
            connectTimeoutTask = Task {
                try? await Task.sleep(nanoseconds: 10_000_000_000)
                if !Task.isCancelled {
                    self.connectContinuation?.resume(throwing: BLEError.timedOut)
                    self.connectContinuation = nil
                }
            }

            if centralManager.state == .poweredOn {
                startScanOrReconnect()
            }
            // If not poweredOn yet, centralManagerDidUpdateState(_:) picks
            // this up once it is.
        }
    }

    private func startScanOrReconnect() {
        if let known = centralManager.retrieveConnectedPeripherals(withServices: [serviceUUID]).first {
            connect(known)
        } else {
            centralManager.scanForPeripherals(withServices: [serviceUUID], options: nil)
        }
    }

    private func connect(_ peripheral: CBPeripheral) {
        self.peripheral = peripheral
        peripheral.delegate = self
        centralManager.connect(peripheral, options: nil)
    }
}

extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn, connectContinuation != nil {
            startScanOrReconnect()
        } else if central.state != .poweredOn, central.state != .unknown {
            connectContinuation?.resume(throwing: BLEError.bluetoothUnavailable)
            connectContinuation = nil
        }
    }

    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        if let peripherals = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
           let restored = peripherals.first {
            peripheral = restored
            restored.delegate = self
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                         advertisementData: [String: Any], rssi RSSI: NSNumber) {
        centralManager.stopScan()
        connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        connectTimeoutTask?.cancel()
        connectContinuation?.resume(throwing: error ?? BLEError.timedOut)
        connectContinuation = nil
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        DispatchQueue.main.async { self.isConnected = false }
        controlChar = nil
    }
}

extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else { return }
        peripheral.discoverCharacteristics([statusCharUUID, controlCharUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for c in service.characteristics ?? [] {
            if c.uuid == controlCharUUID {
                controlChar = c
            }
            if c.uuid == statusCharUUID {
                peripheral.setNotifyValue(true, for: c)
            }
        }

        connectTimeoutTask?.cancel()
        DispatchQueue.main.async { self.isConnected = true }
        connectContinuation?.resume()
        connectContinuation = nil
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let data = characteristic.value, let str = String(data: data, encoding: .utf8) else { return }
        DispatchQueue.main.async { self.lastStatus = str }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            writeContinuation?.resume(throwing: error)
        } else {
            writeContinuation?.resume()
        }
        writeContinuation = nil
    }
}
