// Minimal app shell. The real functionality lives in BLEManager.swift
// (the BLE connection) and LightIntents.swift (the Shortcuts actions) --
// this view just exists so you can manually trigger a command and watch
// the connection/status state while testing, before wiring up Shortcuts.

import SwiftUI

@main
struct SmartRoomLightApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

struct ContentView: View {
    @ObservedObject private var ble = BLEManager.shared
    @State private var errorText = ""

    var body: some View {
        VStack(spacing: 16) {
            Text(ble.isConnected ? "Connected" : "Not connected")
                .font(.headline)
                .foregroundStyle(ble.isConnected ? .green : .secondary)

            Text("Last status: \(ble.lastStatus)")
                .font(.footnote)
                .foregroundStyle(.secondary)

            Button("Toggle Light") {
                Task {
                    do {
                        try await ble.sendCommand(0)
                        errorText = ""
                    } catch {
                        errorText = "\(error)"
                    }
                }
            }
            .buttonStyle(.borderedProminent)

            if !errorText.isEmpty {
                Text(errorText)
                    .font(.footnote)
                    .foregroundStyle(.red)
            }
        }
        .padding()
    }
}
