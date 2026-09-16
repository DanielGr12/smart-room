// One AppIntent per remote button, matching the ESP32 firmware's
// COMMANDS table index-for-index. Each one is auto-published as a
// standalone Shortcuts action via LightShortcuts below -- after
// installing the app once, open the Shortcuts app and these appear
// under the app's name, ready to add to the Home Screen, Siri, or
// Back Tap (Settings > Accessibility > Touch > Back Tap).

import AppIntents

private func run(_ index: UInt8) async throws -> some IntentResult {
    try await BLEManager.shared.sendCommand(index)
    return .result()
}

struct ToggleLightIntent: AppIntent {
    static var title: LocalizedStringResource = "Toggle Light"
    static var description = IntentDescription("Toggles the room light on or off.")
    func perform() async throws -> some IntentResult { try await run(0) }
}

struct Timer1hIntent: AppIntent {
    static var title: LocalizedStringResource = "Light Timer: 1 Hour"
    func perform() async throws -> some IntentResult { try await run(1) }
}

struct Timer2hIntent: AppIntent {
    static var title: LocalizedStringResource = "Light Timer: 2 Hours"
    func perform() async throws -> some IntentResult { try await run(2) }
}

struct Timer4hIntent: AppIntent {
    static var title: LocalizedStringResource = "Light Timer: 4 Hours"
    func perform() async throws -> some IntentResult { try await run(3) }
}

struct Timer8hIntent: AppIntent {
    static var title: LocalizedStringResource = "Light Timer: 8 Hours"
    func perform() async throws -> some IntentResult { try await run(4) }
}

struct FanStopIntent: AppIntent {
    static var title: LocalizedStringResource = "Fan: Stop"
    func perform() async throws -> some IntentResult { try await run(5) }
}

struct FanLowIntent: AppIntent {
    static var title: LocalizedStringResource = "Fan: Low"
    func perform() async throws -> some IntentResult { try await run(6) }
}

struct FanMedIntent: AppIntent {
    static var title: LocalizedStringResource = "Fan: Medium"
    func perform() async throws -> some IntentResult { try await run(7) }
}

struct FanHighIntent: AppIntent {
    static var title: LocalizedStringResource = "Fan: High"
    func perform() async throws -> some IntentResult { try await run(8) }
}

struct LightShortcuts: AppShortcutsProvider {
    static var appShortcuts: [AppShortcut] {
        AppShortcut(intent: ToggleLightIntent(),
                    phrases: ["Toggle the light with \(.applicationName)"],
                    shortTitle: "Toggle Light",
                    systemImageName: "lightbulb")
        AppShortcut(intent: Timer1hIntent(),
                    phrases: ["Set a one hour light timer with \(.applicationName)"],
                    shortTitle: "Timer 1h",
                    systemImageName: "timer")
        AppShortcut(intent: Timer2hIntent(),
                    phrases: ["Set a two hour light timer with \(.applicationName)"],
                    shortTitle: "Timer 2h",
                    systemImageName: "timer")
        AppShortcut(intent: Timer4hIntent(),
                    phrases: ["Set a four hour light timer with \(.applicationName)"],
                    shortTitle: "Timer 4h",
                    systemImageName: "timer")
        AppShortcut(intent: Timer8hIntent(),
                    phrases: ["Set an eight hour light timer with \(.applicationName)"],
                    shortTitle: "Timer 8h",
                    systemImageName: "timer")
        AppShortcut(intent: FanStopIntent(),
                    phrases: ["Stop the fan with \(.applicationName)"],
                    shortTitle: "Fan Stop",
                    systemImageName: "fanblades")
        AppShortcut(intent: FanLowIntent(),
                    phrases: ["Set the fan to low with \(.applicationName)"],
                    shortTitle: "Fan Low",
                    systemImageName: "fanblades")
        AppShortcut(intent: FanMedIntent(),
                    phrases: ["Set the fan to medium with \(.applicationName)"],
                    shortTitle: "Fan Medium",
                    systemImageName: "fanblades")
        AppShortcut(intent: FanHighIntent(),
                    phrases: ["Set the fan to high with \(.applicationName)"],
                    shortTitle: "Fan High",
                    systemImageName: "fanblades")
    }
}
