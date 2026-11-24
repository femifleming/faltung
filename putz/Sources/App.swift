// putz — a cleaner for this mac, in the faltung look.
import SwiftUI
import AppKit

enum Page: String, CaseIterable, Identifiable {
    case health, clean, apps, startup, disk, dupes, leftovers, tune, settings
    var id: String { rawValue }
    var code: String {
        switch self {
        case .health: "hc0"; case .clean: "cl1"; case .apps: "un2"; case .startup: "st3"; case .disk: "dk4"
        case .dupes: "dp5"; case .leftovers: "or6"; case .tune: "mt7"; case .settings: "se8"
        }
    }
    var title: String {
        switch self {
        case .health: "health"; case .clean: "clean"; case .apps: "uninstall"; case .startup: "startup"; case .disk: "disk"
        case .dupes: "duplicates"; case .leftovers: "leftovers"; case .tune: "tune"; case .settings: "settings"
        }
    }
    var subtitle: String {
        switch self {
        case .health: "how this mac is doing, and what to do about it"
        case .clean: "caches, logs, browsers, developer junk — analyse first, then clean"
        case .apps: "remove apps with everything they left in your library"
        case .startup: "what starts when you log in or boot"
        case .disk: "where the space went: folders, largest and forgotten files"
        case .dupes: "the same file, more than once"
        case .leftovers: "files of apps that are gone, and broken preferences"
        case .tune: "maintenance jobs macos hides in the terminal"
        case .settings: "how putz removes things, and what it never touches"
        }
    }
    /// one colour field per page (light ground, light accent) — faltung product pairs
    var colours: (UInt32, UInt32) {
        switch self {
        case .health: (0xc9b53b, 0xf4f2ec)
        case .clean: (0x79e04a, 0x2f4a52)
        case .apps: (0x8ec3e6, 0x6e80c8)
        case .startup: (0x9cc788, 0x5e8f6a)
        case .disk: (0xf4f9fb, 0x9fd3ec)
        case .dupes: (0xf6c590, 0xee8e6e)
        case .leftovers: (0x8d9bd6, 0xcfe6f0)
        case .tune: (0xfbfbf8, 0x2f7fd1)
        case .settings: (0xdfe9ed, 0x8fa9b5)
        }
    }
    var solid: Int { [0, 1, 2, 3, 1, 2, 0, 3, 2][Page.allCases.firstIndex(of: self) ?? 0] }
}

final class Nav: ObservableObject { @Published var page: Page = .health }

@MainActor
final class Store: ObservableObject {
    let cleaner = Cleaner()
    let uninstaller = Uninstaller()
    let orphans = Orphans()
    let startup = Startup()
    let disk = DiskScan()
    let dupes = Duplicates()
    let tune = Maintenance()
}

@main
struct PutzApp: App {
    @NSApplicationDelegateAdaptor(Delegate.self) var delegate
    init() { Selftest.runIfAsked(); Fonts.register() }
    var body: some Scene {
        WindowGroup("putz") {
            Root()
                .frame(minWidth: 1040, minHeight: 680)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1180, height: 760)
        .commands { CommandGroup(replacing: .newItem) {} }
    }
}

final class Delegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ s: NSApplication) -> Bool { true }
}

struct Root: View {
    @StateObject var nav = Nav()
    var start: Page? = nil
    @StateObject var store = Store()
    @ObservedObject var prefs = Prefs.shared
    @Environment(\.colorScheme) var scheme

    var dark: Bool { prefs.appearance == 0 ? scheme == .dark : prefs.appearance == 2 }

    var body: some View {
        let (g, a) = nav.page.colours
        let theme = Theme.of(g, a, dark: dark)
        HStack(spacing: 0) {
            Sidebar(dark: dark)
            ZStack(alignment: .topLeading) {
                theme.ground.ignoresSafeArea()
                VStack(alignment: .leading, spacing: 0) {
                    Header(page: nav.page, busy: busy)
                    page.padding(.horizontal, 36).padding(.bottom, 24)
                }
            }
            .environment(\.theme, theme)
            .foregroundStyle(theme.ink)
        }
        .environmentObject(nav)
        .environmentObject(store)
        .environmentObject(store.cleaner)
        .environmentObject(store.uninstaller)
        .environmentObject(store.orphans)
        .environmentObject(store.startup)
        .environmentObject(store.disk)
        .environmentObject(store.dupes)
        .environmentObject(store.tune)
        .ignoresSafeArea()
        .onAppear {
            if let start { nav.page = start }
            else if let e = ProcessInfo.processInfo.environment["PUTZ_PAGE"], let p = Page.allCases.first(where: { $0.code == e }) { nav.page = p }
            // read-only: analyse on launch (for screenshots / checking)
            if ProcessInfo.processInfo.environment["PUTZ_ANALYSE"] != nil { Task { await store.cleaner.analyse() } }
        }
    }

    var busy: Bool {
        switch nav.page {
        case .health, .clean: store.cleaner.busy
        case .apps: store.uninstaller.loading || store.uninstaller.findingLeftovers
        case .startup: store.startup.busy
        case .disk: store.disk.busy
        case .dupes: store.dupes.busy
        case .leftovers: store.orphans.busy
        case .tune: store.tune.running != nil
        case .settings: false
        }
    }

    @ViewBuilder var page: some View {
        switch nav.page {
        case .health: HealthView()
        case .clean: CleanView()
        case .apps: AppsView()
        case .startup: StartupView()
        case .disk: DiskView()
        case .dupes: DupesView()
        case .leftovers: LeftoversView()
        case .tune: TuneView()
        case .settings: SettingsView()
        }
    }
}

struct Sidebar: View {
    var dark: Bool
    @EnvironmentObject var nav: Nav
    @EnvironmentObject var cleaner: Cleaner
    var body: some View {
        let side = Theme.of(0x1a242c, 0x5aaad8, dark: false)
        VStack(alignment: .leading, spacing: 2) {
            Spacer().frame(height: 46)
            Text("putz").font(.sans(26, medium: true)).foregroundStyle(side.ink).padding(.leading, 22)
            Text("a cleaner for this mac").font(.mono(10)).foregroundStyle(side.dim).padding(.leading, 22).padding(.bottom, 26)
            ForEach(Page.allCases) { p in
                let on = nav.page == p
                let c = Theme.of(p.colours.0, p.colours.1, dark: false).ground
                HStack(spacing: 10) {
                    Text(p.code).font(.mono(10)).foregroundStyle(on ? Color(hex: 0x1d1c15) : side.dim).frame(width: 26, alignment: .leading)
                    Text(p.title).font(.sans(13, medium: on)).foregroundStyle(on ? Color(hex: 0x1d1c15) : side.ink)
                    Spacer()
                    if !on { Chip(color: c) }
                }
                .padding(.horizontal, 14).padding(.vertical, 7)
                .background(on ? AnyView(Skew(skew: 0.35).fill(Theme.of(p.colours.0, p.colours.1, dark: false).ground)) : AnyView(Color.clear))
                .padding(.horizontal, 8)
                .contentShape(Rectangle())
                .onTapGesture { nav.page = p }
            }
            Spacer()
            VStack(alignment: .leading, spacing: 3) {
                Text("freed so far").font(.mono(9)).foregroundStyle(side.dim)
                Text(Fmt.bytes(History.shared.total)).font(.mono(12, medium: true)).foregroundStyle(side.ink)
            }
            .padding(.leading, 22).padding(.bottom, 20)
        }
        .frame(width: 210)
        .background(side.ground)
        .environment(\.theme, side)
    }
}

struct Header: View {
    var page: Page
    var busy: Bool
    @Environment(\.theme) var t
    var body: some View {
        ZStack(alignment: .topLeading) {
            if page == .disk || page == .tune || page == .settings { Chevrons().frame(height: 130).opacity(0.9) }
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(page.code).font(.mono(11)).foregroundStyle(t.dim)
                    Text(page.title).font(.sans(32, medium: true)).tracking(-0.6)
                    Text(page.subtitle).font(.sans(13)).foregroundStyle(t.dim)
                }
                Spacer()
                Polyhedron(spinning: busy, kind: page.solid).frame(width: 96, height: 96)
            }
            .padding(.top, 34).padding(.horizontal, 36)
        }
        .frame(height: 140)
    }
}
