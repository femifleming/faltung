// cl1 clean — the rule set and the engine (analyse, then clean what was found).
import Foundation
import SQLite3
import AppKit

enum RuleGroup: String, CaseIterable {
    case system, browsers, developer, apps, custom
}

/// what one rule looks at
enum Target {
    /// each child of every matched folder (except names in `except`), optionally only those older than N days
    case children(String, age: (() -> Int)? = nil, exts: [String]? = nil, except: [String] = [])
    /// the matched items themselves
    case items(String)
    /// children of ~/Library/Caches that no other rule claims
    case cacheCatchAll
    /// the cache folders inside every Electron / CEF app under Application Support
    case electron
    /// an SQL clean-up of a database (Firefox history keeps bookmarks)
    case sql(String, count: String, statements: [String])
    /// a command; `estimate` is a path whose size is shown
    case command(String, [String], estimate: String?)
    /// old version folders: every match but the newest per key
    case oldVersions(String, key: (String) -> String)
    /// user-added folders in settings: their contents
    case includes
}

struct Rule: Identifiable {
    let id: String
    let group: RuleGroup
    let name: String
    let note: String
    var targets: [Target]
    var defaultOn = true
    var needsFullDisk = false
    var apps: [String] = []            // bundle ids that must not be running
    var appName = ""                   // for the "close x first" note
    var how: Files.Removal? = nil      // forced erase / trash, else the setting
    var visibleIf: String? = nil       // only listed when this path exists
}

/// one thing a rule found
struct Found: Identifiable, Hashable {
    var id: String { path + "|" + kind }
    var path: String
    var bytes: Int64
    var files: Int
    var kind = "path"                  // path · sql · cmd
    var label = ""
}

struct RuleResult {
    var items: [Found] = []
    var skipped: String? = nil
    var bytes: Int64 { items.reduce(0) { $0 + $1.bytes } }
    var files: Int { items.reduce(0) { $0 + $1.files } }
}

// MARK: - the rules

enum Rules {
    static let lib = H("Library"), caches = H("Library/Caches"), support = H("Library/Application Support")

    static func all() -> [Rule] {
        var r: [Rule] = []
        let p = Prefs.shared

        // ---- system
        r.append(Rule(id: "sys.caches", group: .system, name: "user caches",
                      note: "app caches in ~/Library/Caches; apps rebuild them", targets: [.cacheCatchAll]))
        r.append(Rule(id: "sys.logs", group: .system, name: "user logs",
                      note: "~/Library/Logs (age from settings)",
                      targets: [.children(H("Library/Logs"), age: { p.logAgeDays }, except: ["DiagnosticReports"])]))
        r.append(Rule(id: "sys.crash", group: .system, name: "crash reports",
                      note: "diagnostic reports, yours and the system's",
                      targets: [.children(H("Library/Logs/DiagnosticReports")), .children("/Library/Logs/DiagnosticReports")]))
        r.append(Rule(id: "sys.trash", group: .system, name: "trash",
                      note: "empties the bin, on this disk and mounted drives",
                      targets: [.children(H(".Trash")), .children("/Volumes/*/.Trashes/\(getuid())")],
                      needsFullDisk: true, how: .erase))
        r.append(Rule(id: "sys.temp", group: .system, name: "temporary files",
                      note: "your temp folder, items untouched for the days set in settings",
                      targets: [.children((NSTemporaryDirectory() as NSString).standardizingPath, age: { max(1, p.tempAgeDays) })]))
        r.append(Rule(id: "sys.quicklook", group: .system, name: "quick look thumbnails",
                      note: "resets the thumbnail cache (qlmanage -r cache)",
                      targets: [.command("/usr/bin/qlmanage", ["-r", "cache"],
                                         estimate: (userCacheDir as NSString).appendingPathComponent("com.apple.QuickLook.thumbnailcache"))]))
        r.append(Rule(id: "sys.savedstate", group: .system, name: "saved window state",
                      note: "windows apps reopen at launch", targets: [.children(H("Library/Saved Application State"))], defaultOn: false))
        r.append(Rule(id: "sys.recent", group: .system, name: "recent items lists",
                      note: "recent documents, apps and servers (privacy)",
                      targets: [.items(H("Library/Application Support/com.apple.sharedfilelist/com.apple.LSSharedFileList.ApplicationRecentDocuments/*.sfl*")),
                                .items(H("Library/Application Support/com.apple.sharedfilelist/com.apple.LSSharedFileList.Recent*.sfl*"))],
                      defaultOn: false))
        r.append(Rule(id: "sys.mail", group: .system, name: "mail attachments cache",
                      note: "copies of attachments mail opened; the mails keep them",
                      targets: [.children(H("Library/Containers/com.apple.mail/Data/Library/Mail Downloads"))],
                      needsFullDisk: true, apps: ["com.apple.mail"], appName: "mail"))
        r.append(Rule(id: "sys.ios-updates", group: .system, name: "ios software updates",
                      note: "firmware files the finder downloaded for devices",
                      targets: [.children(H("Library/iTunes/iPhone Software Updates")), .children(H("Library/iTunes/iPad Software Updates"))]))
        r.append(Rule(id: "sys.ios-backups", group: .system, name: "ios device backups",
                      note: "local iphone / ipad backups — check you don't need them",
                      targets: [.children(H("Library/Application Support/MobileSync/Backup"))],
                      defaultOn: false, needsFullDisk: true, how: .trash))
        r.append(Rule(id: "sys.installers", group: .system, name: "installers in downloads",
                      note: ".dmg / .pkg / .xip in ~/Downloads older than the days set in settings (to trash)",
                      targets: [.children(H("Downloads"), age: { p.downloadsAgeDays }, exts: ["dmg", "pkg", "mpkg", "xip"])],
                      defaultOn: false, how: .trash))
        r.append(Rule(id: "sys.fonts", group: .system, name: "font caches",
                      note: "rebuilds your font cache (atsutil); fixes garbled type",
                      targets: [.command("/usr/bin/atsutil", ["databases", "-removeUser"], estimate: nil)], defaultOn: false))

        // ---- browsers
        r += safari()
        r += firefox()
        for b in chromiumBrowsers { r += chromium(b) }

        // ---- developer
        let x = H("Library/Developer/Xcode")
        r.append(Rule(id: "dev.derived", group: .developer, name: "xcode derived data", note: "build products and indexes",
                      targets: [.children(x + "/DerivedData")], apps: ["com.apple.dt.Xcode"], appName: "xcode", visibleIf: x))
        r.append(Rule(id: "dev.archives", group: .developer, name: "xcode archives", note: "builds you archived for release",
                      targets: [.children(x + "/Archives")], defaultOn: false, how: .trash, visibleIf: x + "/Archives"))
        r.append(Rule(id: "dev.devicesupport", group: .developer, name: "device support files",
                      note: "symbols for devices you once plugged in; refetched when needed",
                      targets: [.children(x + "/iOS DeviceSupport"), .children(x + "/watchOS DeviceSupport"),
                                .children(x + "/tvOS DeviceSupport"), .children(x + "/visionOS DeviceSupport")],
                      defaultOn: false, visibleIf: x))
        r.append(Rule(id: "dev.simcache", group: .developer, name: "simulator caches", note: "core simulator caches",
                      targets: [.children(H("Library/Developer/CoreSimulator/Caches"))], visibleIf: H("Library/Developer/CoreSimulator")))
        r.append(Rule(id: "dev.simunavail", group: .developer, name: "unavailable simulators",
                      note: "simulators for runtimes no longer installed (simctl delete unavailable)",
                      targets: [.command("/usr/bin/xcrun", ["simctl", "delete", "unavailable"], estimate: nil)],
                      defaultOn: false, visibleIf: H("Library/Developer/CoreSimulator/Devices")))
        let pkgs: [(String, String, [String])] = [
            ("dev.brew", "homebrew downloads", [caches + "/Homebrew"]),
            ("dev.npm", "npm cache", [H(".npm/_cacache")]),
            ("dev.yarn", "yarn / pnpm cache", [caches + "/Yarn", caches + "/pnpm", H("Library/pnpm/store")]),
            ("dev.pip", "pip / uv cache", [caches + "/pip", H(".cache/pip"), H(".cache/uv")]),
            ("dev.go", "go build cache", [caches + "/go-build"]),
            ("dev.cargo", "cargo registry cache", [H(".cargo/registry/cache")]),
            ("dev.pods", "cocoapods cache", [caches + "/CocoaPods"]),
            ("dev.ccache", "ccache / clang module cache", [caches + "/ccache", userCacheDir + "/clang/ModuleCache"]),
            ("dev.swiftpm", "swift package cache", [caches + "/org.swift.swiftpm"]),
            ("dev.gyp", "node-gyp / bun / deno", [caches + "/node-gyp", H(".bun/install/cache"), caches + "/deno"]),
            ("dev.gradle", "gradle caches", [H(".gradle/caches")]),
            ("dev.jetbrains", "jetbrains caches", [caches + "/JetBrains"]),
        ]
        for (id, name, dirs) in pkgs {
            r.append(Rule(id: id, group: .developer, name: name, note: "redownloaded when a build needs it",
                          targets: dirs.map { .children($0) }, defaultOn: id != "dev.gradle", visibleIf: dirs.first(where: Files.exists)))
        }

        // ---- apps
        r.append(Rule(id: "app.electron", group: .apps, name: "electron app caches",
                      note: "web caches of slack, discord, vs code, claude, notion… (any app built on chromium)",
                      targets: [.electron]))
        r.append(Rule(id: "app.spotify", group: .apps, name: "spotify", note: "offline-playback cache that isn't downloads",
                      targets: [.children(caches + "/com.spotify.client"), .children(support + "/Spotify/PersistentCache/Storage")],
                      apps: ["com.spotify.client"], appName: "spotify", visibleIf: support + "/Spotify"))
        r.append(Rule(id: "app.adobe", group: .apps, name: "adobe media cache",
                      note: "premiere / after effects conformed audio and peak files; rebuilt on open",
                      targets: [.children(support + "/Adobe/Common/Media Cache Files"), .children(support + "/Adobe/Common/Media Cache"),
                                .children(support + "/Adobe/Common/Peak Files")],
                      defaultOn: false, apps: ["com.adobe.PremierePro", "com.adobe.AfterEffects", "com.adobe.Audition"], appName: "premiere",
                      visibleIf: support + "/Adobe/Common"))
        r.append(Rule(id: "app.ableton", group: .apps, name: "ableton live crash dumps",
                      note: "crash-recovery sets of every live version, older than a week", targets: [.children(H("Library/Preferences/Ableton/*/Crash"), age: { 7 })],
                      apps: ["com.ableton.live"], appName: "live", visibleIf: H("Library/Preferences/Ableton")))
        r.append(Rule(id: "app.ableton-old", group: .apps, name: "old live version settings",
                      note: "preference folders of live updates you no longer run (newest per major version kept; to trash)",
                      targets: [.oldVersions(H("Library/Preferences/Ableton/Live *"), key: { name in
                          String(name.dropFirst(5).prefix(while: { $0 != "." }))
                      })],
                      defaultOn: false, how: .trash, visibleIf: H("Library/Preferences/Ableton")))
        r.append(Rule(id: "app.teams", group: .apps, name: "microsoft teams", note: "teams web cache",
                      targets: [.children(H("Library/Containers/com.microsoft.teams2/Data/Library/Caches"))],
                      apps: ["com.microsoft.teams2"], appName: "teams", visibleIf: H("Library/Containers/com.microsoft.teams2")))
        r.append(Rule(id: "app.steam", group: .apps, name: "steam web cache", note: "steam's store browser cache",
                      targets: [.children(support + "/Steam/htmlcache"), .children(support + "/Steam/appcache/httpcache")],
                      apps: ["com.valvesoftware.steam"], appName: "steam", visibleIf: support + "/Steam"))
        r.append(Rule(id: "app.docker", group: .apps, name: "docker desktop logs", note: "docker desktop's own logs",
                      targets: [.children(H("Library/Containers/com.docker.docker/Data/log"))],
                      defaultOn: false, apps: ["com.docker.docker"], appName: "docker", visibleIf: H("Library/Containers/com.docker.docker")))

        // ---- custom
        r.append(Rule(id: "custom.include", group: .custom, name: "your folders",
                      note: "contents of the folders added in settings → include", targets: [.includes], defaultOn: false))
        return r.filter { $0.visibleIf == nil || Files.exists($0.visibleIf!) }
    }

    static let userCacheDir: String = {
        let n = confstr(_CS_DARWIN_USER_CACHE_DIR, nil, 0)
        guard n > 0 else { return NSTemporaryDirectory() }
        var buf = [CChar](repeating: 0, count: n)
        confstr(_CS_DARWIN_USER_CACHE_DIR, &buf, n)
        return String(cString: buf).standardizedPath
    }()

    // ---- safari
    static func safari() -> [Rule] {
        let s = H("Library/Safari"), c = H("Library/Containers/com.apple.Safari/Data/Library")
        let ids = ["com.apple.Safari"]
        return [
            Rule(id: "safari.cache", group: .browsers, name: "safari · cache", note: "web pages and images",
                 targets: [.children(caches + "/com.apple.Safari"), .children(c + "/Caches")],
                 needsFullDisk: true, apps: ids, appName: "safari"),
            Rule(id: "safari.history", group: .browsers, name: "safari · history", note: "visited sites, recently closed tabs (synced devices may send it back)",
                 targets: [.items(s + "/History.db*"), .items(s + "/RecentlyClosedTabs.plist"), .items(s + "/LastSession.plist")],
                 needsFullDisk: true, apps: ids, appName: "safari"),
            Rule(id: "safari.cookies", group: .browsers, name: "safari · cookies", note: "signs you out of sites",
                 targets: [.items(c + "/Cookies/Cookies.binarycookies"), .items(H("Library/Cookies/Cookies.binarycookies"))],
                 defaultOn: false, needsFullDisk: true, apps: ids, appName: "safari"),
            Rule(id: "safari.downloads", group: .browsers, name: "safari · download list", note: "the list, not the files",
                 targets: [.items(s + "/Downloads.plist")], needsFullDisk: true, apps: ids, appName: "safari"),
            Rule(id: "safari.sitedata", group: .browsers, name: "safari · website data", note: "local storage and databases of sites",
                 targets: [.children(c + "/WebKit/WebsiteData")], defaultOn: false, needsFullDisk: true, apps: ids, appName: "safari"),
        ]
    }

    // ---- firefox
    static func firefox() -> [Rule] {
        let base = support + "/Firefox/Profiles"
        guard Files.exists(base) else { return [] }
        let prof = base + "/*", ids = ["org.mozilla.firefox"]
        return [
            Rule(id: "firefox.cache", group: .browsers, name: "firefox · cache", note: "web cache, startup and shader caches",
                 targets: [.children(caches + "/Firefox/Profiles/*/cache2"), .children(prof + "/startupCache"),
                           .children(prof + "/shader-cache"), .children(prof + "/thumbnails")],
                 apps: ids, appName: "firefox"),
            Rule(id: "firefox.history", group: .browsers, name: "firefox · history", note: "visits removed, bookmarks kept",
                 targets: [.sql(prof + "/places.sqlite", count: "SELECT COUNT(*) FROM moz_historyvisits",
                                statements: ["DELETE FROM moz_historyvisits",
                                             "DELETE FROM moz_inputhistory",
                                             "DELETE FROM moz_places WHERE id NOT IN (SELECT fk FROM moz_bookmarks WHERE fk NOT NULL)",
                                             "VACUUM"])],
                 apps: ids, appName: "firefox"),
            Rule(id: "firefox.cookies", group: .browsers, name: "firefox · cookies", note: "signs you out of sites",
                 targets: [.items(prof + "/cookies.sqlite*")], defaultOn: false, apps: ids, appName: "firefox"),
            Rule(id: "firefox.session", group: .browsers, name: "firefox · session", note: "saved tabs of the last session",
                 targets: [.items(prof + "/sessionstore.jsonlz4"), .children(prof + "/sessionstore-backups")],
                 defaultOn: false, apps: ids, appName: "firefox"),
            Rule(id: "firefox.forms", group: .browsers, name: "firefox · form history", note: "things typed into forms",
                 targets: [.items(prof + "/formhistory.sqlite*")], defaultOn: false, apps: ids, appName: "firefox"),
        ]
    }

    // ---- chromium family
    struct Browser { let id, name, base, cache: String; let ids: [String]; var flat = false }
    static let chromiumBrowsers: [Browser] = [
        Browser(id: "chrome", name: "chrome", base: "Google/Chrome", cache: "Google/Chrome", ids: ["com.google.Chrome"]),
        Browser(id: "arc", name: "arc", base: "Arc/User Data", cache: "Arc", ids: ["company.thebrowser.Browser"]),
        Browser(id: "brave", name: "brave", base: "BraveSoftware/Brave-Browser", cache: "BraveSoftware/Brave-Browser", ids: ["com.brave.Browser"]),
        Browser(id: "edge", name: "edge", base: "Microsoft Edge", cache: "Microsoft Edge", ids: ["com.microsoft.edgemac"]),
        Browser(id: "vivaldi", name: "vivaldi", base: "Vivaldi", cache: "Vivaldi", ids: ["com.vivaldi.Vivaldi"]),
        Browser(id: "chromium", name: "chromium", base: "Chromium", cache: "Chromium", ids: ["org.chromium.Chromium"]),
        Browser(id: "opera", name: "opera", base: "com.operasoftware.Opera", cache: "com.operasoftware.Opera", ids: ["com.operasoftware.Opera"], flat: true),
        Browser(id: "operagx", name: "opera gx", base: "com.operasoftware.OperaGX", cache: "com.operasoftware.OperaGX", ids: ["com.operasoftware.OperaGX"], flat: true),
    ]
    static var chromiumBases: Set<String> { Set(chromiumBrowsers.map { $0.base.split(separator: "/").first.map(String.init) ?? $0.base }) }

    static func chromium(_ b: Browser) -> [Rule] {
        let base = support + "/" + b.base
        guard Files.exists(base) else { return [] }
        let profiles = b.flat ? [base] : [base + "/Default", base + "/Profile *", base + "/Guest Profile"]
        func each(_ sub: String) -> [String] { profiles.map { $0 + "/" + sub } }
        let n = b.name
        var cache: [Target] = [.children(caches + "/" + b.cache)]
        for s in ["Cache", "Code Cache", "GPUCache", "DawnCache", "DawnGraphiteCache", "DawnWebGPUCache",
                  "Service Worker/CacheStorage", "Service Worker/ScriptCache"] { cache += each(s).map { .children($0) } }
        for s in ["GrShaderCache", "ShaderCache", "GraphiteDawnCache"] { cache.append(.children(base + "/" + s)) }
        let history: [Target] = ["History", "History-journal", "Visited Links", "Top Sites", "Top Sites-journal",
                                 "Shortcuts", "Shortcuts-journal", "Network Action Predictor", "Network Action Predictor-journal"]
            .flatMap(each).map { .items($0) }
        let cookies: [Target] = ["Cookies", "Cookies-journal", "Network/Cookies", "Network/Cookies-journal", "Extension Cookies"]
            .flatMap(each).map { .items($0) }
        let sessions: [Target] = ["Sessions", "Current Session", "Current Tabs", "Last Session", "Last Tabs"].flatMap(each).map { .items($0) }
        let site: [Target] = ["Local Storage", "IndexedDB", "Session Storage", "databases"].flatMap(each).map { .children($0) }
        return [
            Rule(id: b.id + ".cache", group: .browsers, name: "\(n) · cache", note: "web cache, code and gpu caches",
                 targets: cache, apps: b.ids, appName: n),
            Rule(id: b.id + ".history", group: .browsers, name: "\(n) · history", note: "visited sites, typed urls, top sites",
                 targets: history, apps: b.ids, appName: n),
            Rule(id: b.id + ".cookies", group: .browsers, name: "\(n) · cookies", note: "signs you out of sites",
                 targets: cookies, defaultOn: false, apps: b.ids, appName: n),
            Rule(id: b.id + ".session", group: .browsers, name: "\(n) · session", note: "tabs it would reopen",
                 targets: sessions, defaultOn: false, apps: b.ids, appName: n),
            Rule(id: b.id + ".sitedata", group: .browsers, name: "\(n) · site storage", note: "local storage, indexeddb of sites",
                 targets: site, defaultOn: false, apps: b.ids, appName: n),
        ]
    }

    /// names under ~/Library/Caches the catch-all leaves alone
    static let cacheKeep: Set<String> = ["CloudKit", "com.apple.bird", "com.apple.HomeKit", "com.apple.containermanagerd",
                                         "FamilyCircle", "familycircled", "com.apple.akd", "com.apple.ap.adprivacyd",
                                         "com.apple.nsurlsessiond", "com.apple.appstoreagent", "com.apple.iCloudNotificationAgent",
                                         "com.apple.AppleMediaServices", "com.apple.amsaccountsd", "com.apple.appleaccountd",
                                         "com.apple.findmy.findmylocateagent", "com.apple.cache_delete", "com.apple.Music",
                                         "com.apple.iTunesCloud", "com.apple.itunescloudd", "GeoServices", "putz", "com.putz.app",
                                         "com.apple.FileProviderDaemon.AppStoreService", "com.apple.managedappdistributionagent"]

    /// first path component under ~/Library/Caches of every target another rule has
    static func claimedCaches(_ rules: [Rule]) -> Set<String> {
        var s: Set<String> = []
        for r in rules {
            for t in r.targets {
                var pat: String?
                switch t {
                case .children(let p, _, _, _), .items(let p): pat = p
                default: break
                }
                if let p = pat, p.hasPrefix(caches + "/") {
                    if let first = p.dropFirst(caches.count + 1).split(separator: "/").first { s.insert(String(first)) }
                }
            }
        }
        return s
    }
}

// MARK: - engine

@MainActor
final class Cleaner: ObservableObject {
    @Published var rules: [Rule] = Rules.all()
    @Published var results: [String: RuleResult] = [:]
    @Published var unpicked: Set<String> = []          // Found ids the user unticked
    @Published var busy = false
    @Published var status = "ready"
    @Published var progress = 0.0
    @Published var lastCleaned: (bytes: Int64, items: Int, failed: Int)? = nil
    @Published var failures: [String] = []
    @Published var analysed = false
    private var cancelFlag = false

    func isOn(_ r: Rule) -> Bool { Prefs.shared.ruleState[r.id] ?? r.defaultOn }
    func set(_ r: Rule, _ on: Bool) { Prefs.shared.ruleState[r.id] = on; objectWillChange.send() }
    func setGroup(_ g: RuleGroup, _ on: Bool) { for r in rules where r.group == g { Prefs.shared.ruleState[r.id] = on }; objectWillChange.send() }
    func reloadRules() { rules = Rules.all() }

    /// ticked items of ticked rules, an item inside another ticked item counted once
    var totalFound: Int64 {
        let items = rules.filter(isOn).flatMap { r in (results[r.id]?.items ?? []).filter { !unpicked.contains($0.id) } }
        let dirs = Set(items.filter { $0.kind == "path" }.map(\.path))
        return items.reduce(0) { acc, f in
            guard f.kind == "path" else { return acc + f.bytes }
            var p = (f.path as NSString).deletingLastPathComponent
            while p.count > 1 { if dirs.contains(p) { return acc }; p = (p as NSString).deletingLastPathComponent }
            return acc + f.bytes
        }
    }
    func cancel() { cancelFlag = true }

    func analyse(onlyOn: Bool = true) async {
        guard !busy else { return }
        busy = true; cancelFlag = false; analysed = false; lastCleaned = nil
        reloadRules()
        let todo = rules.filter { !onlyOn || isOn($0) }
        let claimed = Rules.claimedCaches(rules)
        let skipRunning = Prefs.shared.skipRunning
        let fda = Access.fullDisk
        results = [:]; unpicked = []
        for (i, r) in todo.enumerated() {
            if cancelFlag { break }
            status = "analysing \(r.name)"
            progress = Double(i) / Double(max(1, todo.count))
            var res = RuleResult()
            if r.needsFullDisk && !fda {
                res.skipped = "needs full disk access"
            } else if skipRunning, !r.apps.isEmpty, let app = Running.isRunning(r.apps) {
                res.skipped = "close \(app) first"
            } else {
                let rule = r
                res.items = await Task.detached(priority: .userInitiated) { Cleaner.scan(rule, claimed: claimed) }.value
            }
            results[r.id] = res
        }
        progress = 1
        analysed = true
        status = cancelFlag ? "stopped" : "found \(Fmt.bytes(totalFound)) in \(Fmt.count(rules.filter(isOn).compactMap { results[$0.id]?.items.count }.reduce(0, +))) items"
        busy = false
    }

    nonisolated static func scan(_ r: Rule, claimed: Set<String>) -> [Found] {
        var out: [Found] = []
        var seen: Set<String> = []
        func add(_ path: String) {
            let p = path.standardizedPath
            guard !seen.contains(p), Safety.check(p) == nil else { return }
            seen.insert(p)
            let s = Files.size(p)
            if s.files == 0 && s.bytes == 0 && Files.isDir(p) {
                // an empty folder still counts as clutter, but only inside cache-like places
                out.append(Found(path: p, bytes: 0, files: 1)); return
            }
            out.append(Found(path: p, bytes: s.bytes, files: max(1, s.files)))
        }
        for t in r.targets {
            switch t {
            case .children(let pat, let age, let exts, let except):
                for dir in Files.glob(pat) where Files.isDir(dir) && !Files.isSymlink(dir) {
                    let days = age?() ?? 0
                    for c in Files.children(dir) {
                        let name = (c as NSString).lastPathComponent
                        if except.contains(name) || name == ".DS_Store" { continue }
                        if let exts, !exts.contains((c as NSString).pathExtension.lowercased()) { continue }
                        if days > 0 {
                            let newest = Files.isDir(c) ? Files.newestInside(c) : Files.modified(c)
                            if let n = newest, -n.timeIntervalSinceNow < Double(days) * 86400 { continue }
                        }
                        add(c)
                    }
                }
            case .items(let pat):
                for p in Files.glob(pat) { add(p) }
            case .cacheCatchAll:
                let running = Set(NSWorkspace.shared.runningApplications.compactMap(\.bundleIdentifier))
                let skip = Prefs.shared.skipRunning
                for c in Files.children(Rules.caches) {
                    let name = (c as NSString).lastPathComponent
                    if claimed.contains(name) || Rules.cacheKeep.contains(name) { continue }
                    if skip && running.contains(name) { continue }
                    add(c)
                }
            case .electron:
                let skipBases = Rules.chromiumBases.union(["Firefox", "Google", "Arc", "Caches", "CrashReporter"])
                let running = NSWorkspace.shared.runningApplications.compactMap { $0.localizedName?.lowercased() }
                let skip = Prefs.shared.skipRunning
                var roots = Files.children(Rules.support)
                roots += Files.glob(H("Library/Containers/*/Data/Library/Application Support/*"))
                for app in roots where Files.isDir(app) {
                    let name = (app as NSString).lastPathComponent
                    if skipBases.contains(name) { continue }
                    if skip, running.contains(where: { $0 == name.lowercased() || $0.replacingOccurrences(of: " ", with: "") == name.lowercased() }) { continue }
                    // only apps that look like chromium inside
                    guard ["GPUCache", "Code Cache", "Local Storage", "blob_storage", "Network"].contains(where: { Files.exists(app + "/" + $0) }) else { continue }
                    for sub in ["Cache", "Code Cache", "GPUCache", "DawnCache", "DawnGraphiteCache", "DawnWebGPUCache",
                                "Service Worker/CacheStorage", "Service Worker/ScriptCache", "CachedData", "CachedExtensionVSIXs",
                                "Crashpad/completed", "logs"] {
                        let d = app + "/" + sub
                        guard Files.isDir(d) else { continue }
                        // "logs" only with its electron siblings, not any app's logs folder
                        if sub == "logs" && !Files.exists(app + "/Code Cache") { continue }
                        for c in Files.children(d) where (c as NSString).lastPathComponent != ".DS_Store" { add(c) }
                    }
                }
            case .sql(let pat, let count, _):
                for db in Files.glob(pat) {
                    let n = SQL.count(db, count)
                    if n > 0 {
                        out.append(Found(path: db, bytes: Files.size(db).bytes, files: n, kind: "sql", label: "\(Fmt.count(n)) visits"))
                    }
                }
            case .command(_, _, let est):
                let s = est.map { Files.size($0) } ?? (0, 1)
                out.append(Found(path: est ?? "", bytes: s.bytes, files: 1, kind: "cmd", label: "run"))
            case .oldVersions(let pat, let key):
                var groups: [String: [String]] = [:]
                for p in Files.glob(pat) where Files.isDir(p) { groups[key((p as NSString).lastPathComponent), default: []].append(p) }
                for (_, list) in groups where list.count > 1 {
                    // newest version number first ("12.4.16" > "12.4.7")
                    let sorted = list.sorted { ($0 as NSString).lastPathComponent.compare(($1 as NSString).lastPathComponent, options: .numeric) == .orderedDescending }
                    for p in sorted.dropFirst() { add(p) }
                }
            case .includes:
                for dir in Prefs.shared.includes where Files.isDir(dir) {
                    for c in Files.children(dir) { add(c) }
                }
            }
        }
        return out.sorted { $0.bytes > $1.bytes }
    }

    func clean() async {
        guard !busy else { return }
        if !analysed { await analyse() }
        busy = true; cancelFlag = false; failures = []
        let prefHow: Files.Removal = Prefs.shared.useTrash ? .trash : .erase
        let todo = rules.filter(isOn).filter { results[$0.id]?.skipped == nil }
        var freed: Int64 = 0, count = 0, failed = 0
        for (i, r) in todo.enumerated() {
            if cancelFlag { break }
            status = "cleaning \(r.name)"
            progress = Double(i) / Double(max(1, todo.count))
            let items = (results[r.id]?.items ?? []).filter { !unpicked.contains($0.id) }
            let how = r.how ?? prefHow
            let rule = r
            let out = await Task.detached(priority: .userInitiated) { Cleaner.apply(rule, items, how: how) }.value
            freed += out.bytes; count += out.count; failed += out.failed.count
            failures += out.failed
            var left = results[r.id] ?? RuleResult()
            left.items = left.items.filter { f in out.leftover.contains(f.id) }
            results[r.id] = left
        }
        if rules.contains(where: { $0.id == "sys.recent" && isOn($0) }) { Shell.run("/usr/bin/killall", ["sharedfilelistd"]) }
        progress = 1
        lastCleaned = (freed, count, failed)
        History.shared.add("clean · \(todo.count) rules", bytes: freed, items: count)
        status = "cleaned \(Fmt.bytes(freed))" + (failed > 0 ? " · \(failed) left (in use or no permission)" : "")
        busy = false
    }

    nonisolated static func apply(_ r: Rule, _ items: [Found], how: Files.Removal) -> (bytes: Int64, count: Int, failed: [String], leftover: Set<String>) {
        var bytes: Int64 = 0, n = 0, failed: [String] = [], left: Set<String> = []
        for f in items {
            switch f.kind {
            case "sql":
                for t in r.targets {
                    if case .sql(_, _, let stmts) = t {
                        let before = Files.size(f.path).bytes
                        if let err = SQL.exec(f.path, stmts) { failed.append("\(Fmt.tilde(f.path)): \(err)"); left.insert(f.id) }
                        else { bytes += max(0, before - Files.size(f.path).bytes); n += f.files }
                    }
                }
            case "cmd":
                for t in r.targets {
                    if case .command(let exe, let args, _) = t {
                        let res = Shell.run(exe, args)
                        if res.status == 0 { bytes += f.bytes; n += 1 } else { failed.append("\(exe): \(res.out.prefix(120))") }
                    }
                }
            default:
                // already gone with an earlier rule's folder
                if !Files.exists(f.path) && !Files.isSymlink(f.path) { continue }
                if let err = Files.remove(f.path, how: how) {
                    // couldn't take the folder whole: take what we can inside it
                    if how == .erase, Files.isDir(f.path), Safety.check(f.path) == nil {
                        let before = f.bytes
                        removeContents(f.path)
                        let after = Files.size(f.path).bytes
                        bytes += max(0, before - after)
                        if after > 0 { failed.append("\(Fmt.tilde(f.path)): \(err)"); left.insert(f.id) } else { n += f.files }
                    } else {
                        failed.append("\(Fmt.tilde(f.path)): \(err)"); left.insert(f.id)
                    }
                } else { bytes += f.bytes; n += f.files }
            }
        }
        return (bytes, n, failed, left)
    }

    nonisolated static func removeContents(_ dir: String) {
        for c in Files.children(dir) {
            if Files.remove(c, how: .erase) != nil, Files.isDir(c), !Files.isSymlink(c) { removeContents(c) }
        }
    }
}

// MARK: - sqlite

enum SQL {
    static func count(_ path: String, _ q: String) -> Int {
        var db: OpaquePointer?
        guard sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, nil) == SQLITE_OK else { sqlite3_close(db); return 0 }
        defer { sqlite3_close(db) }
        sqlite3_busy_timeout(db, 1500)
        var st: OpaquePointer?
        guard sqlite3_prepare_v2(db, q, -1, &st, nil) == SQLITE_OK else { return 0 }
        defer { sqlite3_finalize(st) }
        return sqlite3_step(st) == SQLITE_ROW ? Int(sqlite3_column_int64(st, 0)) : 0
    }
    static func exec(_ path: String, _ stmts: [String]) -> String? {
        var db: OpaquePointer?
        guard sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nil) == SQLITE_OK else {
            let e = String(cString: sqlite3_errmsg(db)); sqlite3_close(db); return e
        }
        defer { sqlite3_close(db) }
        sqlite3_busy_timeout(db, 3000)
        for s in stmts {
            if sqlite3_exec(db, s, nil, nil, nil) != SQLITE_OK { return String(cString: sqlite3_errmsg(db)) }
        }
        return nil
    }
}
