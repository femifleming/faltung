// un2 apps (uninstaller), or6 leftovers (orphans + broken preferences), st3 startup.
import Foundation
import AppKit
import CoreServices

// MARK: - installed apps

struct AppInfo: Identifiable, Hashable {
    var id: String { path }
    var name: String
    var bundleID: String
    var version: String
    var path: String
    var size: Int64? = nil
    var lastUsed: Date? = nil
    var system = false     // root-owned / installed by a package: removal asks for a password
    static func == (a: AppInfo, b: AppInfo) -> Bool { a.path == b.path && a.size == b.size && a.lastUsed == b.lastUsed }
    func hash(into h: inout Hasher) { h.combine(path) }
}

struct Leftover: Identifiable, Hashable {
    var id: String { path }
    var path: String
    var bytes: Int64
    var why: String
    var picked: Bool
    var admin: Bool
}

enum AppScan {
    static let roots = ["/Applications", H("Applications")]

    static func appPaths() -> [String] {
        var out: [String] = []
        func walk(_ dir: String, depth: Int) {
            for c in Files.children(dir) {
                if Files.isSymlink(c) { continue }
                if c.hasSuffix(".app") { out.append(c) }
                else if depth < 2 && Files.isDir(c) && !c.hasSuffix(".bundle") && !c.hasSuffix(".framework") { walk(c, depth: depth + 1) }
            }
        }
        for r in roots { walk(r, depth: 0) }
        return out.sorted { ($0 as NSString).lastPathComponent.lowercased() < ($1 as NSString).lastPathComponent.lowercased() }
    }

    static func info(_ path: String) -> AppInfo? {
        guard let b = Bundle(path: path) else { return nil }
        let id = b.bundleIdentifier ?? ""
        let name = (b.infoDictionary?["CFBundleDisplayName"] as? String)
            ?? (b.infoDictionary?["CFBundleName"] as? String)
            ?? ((path as NSString).lastPathComponent as NSString).deletingPathExtension
        let v = (b.infoDictionary?["CFBundleShortVersionString"] as? String) ?? (b.infoDictionary?["CFBundleVersion"] as? String) ?? ""
        let owner = (try? FileManager.default.attributesOfItem(atPath: path)[.ownerAccountID] as? NSNumber)?.intValue ?? 0
        var a = AppInfo(name: name, bundleID: id, version: v, path: path, system: owner == 0 && !FileManager.default.isWritableFile(atPath: (path as NSString).deletingLastPathComponent))
        if owner == 0 { a.system = !FileManager.default.isWritableFile(atPath: path) }
        if let item = MDItemCreate(nil, path as CFString), let d = MDItemCopyAttribute(item, kMDItemLastUsedDate) as? Date { a.lastUsed = d }
        return a
    }

    /// every bundle id that is installed anywhere the system knows (for the orphan finder)
    static func installedIDs() -> Set<String> {
        var s: Set<String> = []
        var dirs = roots + ["/System/Applications", "/System/Applications/Utilities", "/System/Library/CoreServices",
                            "/Library/Application Support", H("Library/Application Support")]
        dirs += Files.glob("/Applications/*").filter { Files.isDir($0) && !$0.hasSuffix(".app") }
        for d in dirs {
            for c in Files.children(d) where c.hasSuffix(".app") {
                if let id = Bundle(path: c)?.bundleIdentifier { s.insert(id.lowercased()) }
                // helpers and extensions inside the bundle
                for sub in ["Contents/Library/LoginItems", "Contents/PlugIns", "Contents/Library/LaunchServices", "Contents/Helpers", "Contents/Frameworks"] {
                    for h in Files.children(c + "/" + sub) {
                        if let id = Bundle(path: h)?.bundleIdentifier { s.insert(id.lowercased()) }
                    }
                }
            }
        }
        // anything else launch services can find
        for p in appPaths() { if let id = Bundle(path: p)?.bundleIdentifier { s.insert(id.lowercased()) } }
        return s
    }

    static func leftovers(for a: AppInfo) -> [Leftover] {
        var out: [Leftover] = []
        var seen: Set<String> = []
        let id = a.bundleID, name = a.name
        let lib = H("Library")
        func add(_ p: String, _ why: String, picked: Bool = true, admin: Bool = false) {
            let s = p.standardizedPath
            guard !seen.contains(s), Files.exists(s) || Files.isSymlink(s), Safety.check(s) == nil, s != a.path else { return }
            seen.insert(s)
            out.append(Leftover(path: s, bytes: Files.size(s).bytes, why: why, picked: picked, admin: admin))
        }
        func byName(_ dir: String, _ why: String, picked: Bool = true, admin: Bool = false) {
            guard name.count >= 4 else { return }
            for c in Files.children(dir) where (c as NSString).lastPathComponent.lowercased() == name.lowercased() { add(c, why, picked: picked, admin: admin) }
        }
        if !id.isEmpty {
            let lid = id.lowercased()
            func byID(_ dir: String, _ why: String, admin: Bool = false, prefixOK: Bool = true, picked: Bool = true) {
                for c in Files.children(dir) {
                    let n = (c as NSString).lastPathComponent.lowercased()
                    let stem = n.replacingOccurrences(of: ".plist", with: "").replacingOccurrences(of: ".savedstate", with: "")
                        .replacingOccurrences(of: ".binarycookies", with: "")
                    if stem == lid || (prefixOK && stem.hasPrefix(lid + ".")) { add(c, why, picked: picked, admin: admin) }
                }
            }
            byID(lib + "/Application Support", "support files")
            byID(lib + "/Caches", "cache")
            byID(lib + "/Preferences", "preferences")
            byID(lib + "/Preferences/ByHost", "preferences (this mac)")
            byID(lib + "/Containers", "sandbox container")
            byID(lib + "/Application Scripts", "scripts")
            byID(lib + "/Saved Application State", "window state")
            byID(lib + "/HTTPStorages", "web storage")
            byID(lib + "/WebKit", "webkit data")
            byID(lib + "/Cookies", "cookies")
            byID(lib + "/Logs", "logs")
            byID(lib + "/LaunchAgents", "launch agent")
            byID(Rules.userCacheDir, "system cache")
            for c in Files.children(lib + "/Group Containers") {
                let n = (c as NSString).lastPathComponent.lowercased()
                if n.hasSuffix("." + lid) || n == "group." + lid { add(c, "group container") }
            }
            byID("/Library/LaunchAgents", "launch agent (all users)", admin: true)
            byID("/Library/LaunchDaemons", "launch daemon", admin: true)
            byID("/Library/PrivilegedHelperTools", "privileged helper", admin: true)
            byID("/Library/Preferences", "preferences (all users)", admin: true)
        }
        byName(lib + "/Application Support", "support files")
        byName(lib + "/Caches", "cache")
        byName(lib + "/Logs", "logs")
        byName("/Library/Application Support", "support files (all users)", picked: false, admin: true)
        // audio plug-ins of the same name (often the same product)
        for dir in [lib + "/Audio/Plug-Ins", "/Library/Audio/Plug-Ins"] {
            for fmt in ["VST3", "Components", "VST", "CLAP"] {
                for c in Files.children(dir + "/" + fmt) {
                    let stem = ((c as NSString).lastPathComponent as NSString).deletingPathExtension.lowercased()
                    if stem == name.lowercased() { add(c, "audio plug-in (\(fmt.lowercased()))", picked: false, admin: dir.hasPrefix("/Library")) }
                }
            }
        }
        return out.sorted { $0.bytes > $1.bytes }
    }
}

@MainActor
final class Uninstaller: ObservableObject {
    @Published var apps: [AppInfo] = []
    @Published var loading = false
    @Published var selected: AppInfo? = nil
    @Published var leftovers: [Leftover] = []
    @Published var findingLeftovers = false
    @Published var status = ""
    @Published var filter = ""
    @Published var sort = 0   // 0 name · 1 size · 2 last used

    func load() async {
        guard !loading else { return }
        loading = true; status = "listing apps"
        let list = await Task.detached { AppScan.appPaths().compactMap(AppScan.info) }.value
        apps = list
        status = "\(list.count) apps"
        // sizes in the background, a few at a time
        for i in apps.indices {
            let p = apps[i].path
            let s = await Task.detached(priority: .utility) { Files.size(p).bytes }.value
            if i < apps.count && apps[i].path == p { apps[i].size = s }
        }
        status = "\(apps.count) apps · \(Fmt.bytes(apps.compactMap(\.size).reduce(0, +)))"
        loading = false
    }

    var shown: [AppInfo] {
        let f = filter.lowercased()
        let l = f.isEmpty ? apps : apps.filter { $0.name.lowercased().contains(f) || $0.bundleID.lowercased().contains(f) }
        switch sort {
        case 1: return l.sorted { ($0.size ?? 0) > ($1.size ?? 0) }
        case 2: return l.sorted { ($0.lastUsed ?? .distantPast) < ($1.lastUsed ?? .distantPast) }
        default: return l.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        }
    }

    func select(_ a: AppInfo) async {
        selected = a; leftovers = []; findingLeftovers = true
        let l = await Task.detached { AppScan.leftovers(for: a) }.value
        if selected?.path == a.path { leftovers = l }
        findingLeftovers = false
    }

    func uninstall() async {
        guard let a = selected else { return }
        if a.bundleID.hasPrefix("com.apple.") && (a.path.hasPrefix("/System") || a.name == "Safari") { status = "apple system apps stay"; return }
        // quit it first
        for r in NSRunningApplication.runningApplications(withBundleIdentifier: a.bundleID) {
            r.terminate()
            for _ in 0..<40 where !r.isTerminated { try? await Task.sleep(nanoseconds: 100_000_000) }
            if !r.isTerminated { status = "\(a.name) is still running — quit it first"; return }
        }
        status = "removing \(a.name)"
        var freed: Int64 = 0, n = 0, failed: [String] = []
        let total = (a.size ?? 0)
        let appOK = await Task.detached { () -> Bool in
            if Files.remove(a.path, how: .trash) == nil { return true }
            // root-owned: let the finder do it (it asks for the password)
            let r = Shell.osascript("tell application \"Finder\" to delete (POSIX file \(Uninstaller.asQuote(a.path)) as alias)")
            return r.status == 0
        }.value
        if appOK { freed += total; n += 1 } else { failed.append(a.name) }
        let picked = leftovers.filter(\.picked)
        let user = picked.filter { !$0.admin }, admin = picked.filter(\.admin)
        for l in user {
            let r = await Task.detached { Files.remove(l.path, how: .trash) }.value
            if r == nil { freed += l.bytes; n += 1 } else { failed.append(Fmt.tilde(l.path)) }
        }
        if !admin.isEmpty {
            // one password prompt for all of them; moved to your bin so it can be undone
            let dest = H(".Trash")
            var cmd = ""
            for l in admin {
                if l.path.contains("LaunchDaemons") || l.path.contains("LaunchAgents") {
                    cmd += "/bin/launchctl bootout system \(Shell.quote(l.path)) 2>/dev/null; "
                }
                cmd += "/bin/mv -f \(Shell.quote(l.path)) \(Shell.quote(dest + "/" + (l.path as NSString).lastPathComponent + " " + String(Int(Date().timeIntervalSince1970)))); "
            }
            let r = await Task.detached { Shell.admin(cmd, prompt: "putz needs your password to remove \(a.name)'s system files.") }.value
            if r.status == 0 { freed += admin.reduce(0) { $0 + $1.bytes }; n += admin.count } else { failed += admin.map { Fmt.tilde($0.path) } }
        }
        History.shared.add("uninstall · \(a.name)", bytes: freed, items: n)
        status = failed.isEmpty ? "removed \(a.name) · \(Fmt.bytes(freed)) to the bin"
                                : "removed \(n) items · couldn't remove: \(failed.prefix(3).joined(separator: ", "))"
        if appOK { apps.removeAll { $0.path == a.path }; selected = nil; leftovers = [] }
        else { await select(a) }
    }

    nonisolated static func asQuote(_ s: String) -> String {
        "\"" + s.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"") + "\""
    }
}

// MARK: - orphans

struct Orphan: Identifiable, Hashable {
    var id: String { path }
    var path: String
    var bundleID: String
    var place: String
    var bytes: Int64?
    var modified: Date?
    var picked = false
}

@MainActor
final class Orphans: ObservableObject {
    @Published var items: [Orphan] = []
    @Published var broken: [Orphan] = []
    @Published var busy = false
    @Published var status = "finds what apps you deleted left behind"
    @Published var scanned = false

    nonisolated static let places: [(String, String)] = [
        ("Library/Application Support", "support"), ("Library/Caches", "cache"), ("Library/Preferences", "preferences"),
        ("Library/Containers", "container"), ("Library/Group Containers", "group container"),
        ("Library/Saved Application State", "window state"), ("Library/HTTPStorages", "web storage"),
        ("Library/WebKit", "webkit"), ("Library/Application Scripts", "scripts"), ("Library/LaunchAgents", "launch agent"),
        ("Library/Logs", "logs"), ("Library/Cookies", "cookies"),
    ]

    /// reverse-dns name → bundle id, else nil
    nonisolated static func bundleID(from name: String, place: String) -> String? {
        var n = name
        for suf in [".plist", ".savedState", ".binarycookies"] where n.hasSuffix(suf) { n = String(n.dropLast(suf.count)) }
        if place == "group container" {
            if n.hasPrefix("group.") { n = String(n.dropFirst(6)) }
            else if let dot = n.firstIndex(of: "."), n[..<dot].count == 10, n[..<dot].allSatisfy({ $0.isUppercase || $0.isNumber }) {
                n = String(n[n.index(after: dot)...])
            }
        }
        let parts = n.split(separator: ".")
        guard parts.count >= 3, let tld = parts.first?.lowercased(),
              ["com", "org", "net", "io", "de", "co", "app", "us", "uk", "fr", "nl", "jp", "me", "dev", "ai", "tv", "info", "ch", "se", "at", "be", "it", "ru", "cn", "ca", "au", "edu"].contains(tld),
              n.allSatisfy({ $0.isLetter || $0.isNumber || ".-_".contains($0) }) else { return nil }
        return n
    }

    func scan() async {
        guard !busy else { return }
        busy = true; status = "collecting installed apps"; items = []; broken = []
        let fda = Access.fullDisk
        let found = await Task.detached(priority: .userInitiated) { () -> ([Orphan], [Orphan]) in
            let installed = AppScan.installedIDs()
            let running = Set(NSWorkspace.shared.runningApplications.compactMap { $0.bundleIdentifier?.lowercased() })
            let vendors = Set(installed.map { $0.split(separator: ".").prefix(2).joined(separator: ".") })
            var out: [Orphan] = [], bad: [Orphan] = []
            for (rel, place) in Orphans.places {
                let dir = H(rel)
                for c in Files.children(dir) {
                    let name = (c as NSString).lastPathComponent
                    guard let id = Orphans.bundleID(from: name, place: place) else { continue }
                    let l = id.lowercased()
                    if l.hasPrefix("com.apple.") || l.hasPrefix("apple.") || l.hasPrefix("group.com.apple.") { continue }
                    if installed.contains(l) || running.contains(l) { continue }
                    if installed.contains(where: { l.hasPrefix($0 + ".") || $0.hasPrefix(l + ".") }) { continue }
                    // same vendor still installed: probably its helper — left alone
                    if vendors.contains(l.split(separator: ".").prefix(2).joined(separator: ".")) { continue }
                    if NSWorkspace.shared.urlForApplication(withBundleIdentifier: id) != nil { continue }
                    if Safety.check(c) != nil { continue }
                    let sized = place != "container" || fda
                    out.append(Orphan(path: c, bundleID: id, place: place, bytes: sized ? Files.size(c).bytes : nil, modified: Files.modified(c)))
                }
            }
            // preferences that no longer parse
            for c in Files.children(H("Library/Preferences")) where c.hasSuffix(".plist") {
                guard let d = FileManager.default.contents(atPath: c) else { continue }
                if d.isEmpty || (try? PropertyListSerialization.propertyList(from: d, options: [], format: nil)) == nil {
                    bad.append(Orphan(path: c, bundleID: (c as NSString).lastPathComponent, place: "broken preferences",
                                      bytes: Int64(d.count), modified: Files.modified(c), picked: true))
                }
            }
            return (out.sorted { ($0.bytes ?? 0) > ($1.bytes ?? 0) }, bad)
        }.value
        items = found.0; broken = found.1
        scanned = true
        status = "\(items.count) leftovers of removed apps · \(Fmt.bytes(items.compactMap(\.bytes).reduce(0, +))) · \(broken.count) broken preference files"
        busy = false
    }

    func removePicked() async {
        let picked = (items + broken).filter(\.picked)
        guard !picked.isEmpty else { return }
        busy = true
        var freed: Int64 = 0, n = 0, failed = 0
        for o in picked {
            if o.place == "launch agent" { Shell.run("/bin/launchctl", ["bootout", "gui/\(getuid())", o.path]) }
            let r = await Task.detached { Files.remove(o.path, how: .trash) }.value
            if r == nil { freed += o.bytes ?? 0; n += 1 } else { failed += 1 }
        }
        History.shared.add("leftovers", bytes: freed, items: n)
        let gone = Set(picked.map(\.path)).filter { !Files.exists($0) }
        items.removeAll { gone.contains($0.path) }; broken.removeAll { gone.contains($0.path) }
        status = "moved \(n) to the bin · \(Fmt.bytes(freed))" + (failed > 0 ? " · \(failed) couldn't be moved" : "")
        busy = false
    }
}

// MARK: - startup

struct LaunchItem: Identifiable, Hashable {
    enum Domain: String { case login = "login item", agent = "agent", globalAgent = "agent · all users", daemon = "daemon · system" }
    var id: String { domain.rawValue + "|" + (path.isEmpty ? label : path) }
    var label: String
    var path: String          // plist (or app for login items)
    var program: String
    var domain: Domain
    var runAtLoad = false
    var keepAlive = false
    var disabled = false
    var loaded = false
    var vendorMissing = false // its program no longer exists
}

@MainActor
final class Startup: ObservableObject {
    @Published var items: [LaunchItem] = []
    @Published var busy = false
    @Published var status = ""
    @Published var log: [String] = []

    nonisolated static var uid: String { String(getuid()) }

    func load() async {
        busy = true; status = "reading launchd"
        items = await Task.detached { Startup.collect() }.value
        let off = items.filter(\.disabled).count
        status = "\(items.count) items · \(off) disabled · \(items.filter(\.vendorMissing).count) point at missing programs"
        busy = false
    }

    nonisolated static func collect() -> [LaunchItem] {
        var out: [LaunchItem] = []
        let userList = Shell.run("/bin/launchctl", ["list"]).out
        var loadedLabels: Set<String> = []
        for line in userList.split(separator: "\n").dropFirst() {
            let cols = line.split(separator: "\t")
            if cols.count >= 3 { loadedLabels.insert(String(cols[2])) }
        }
        func disabledSet(_ domain: String) -> Set<String> {
            var s: Set<String> = []
            for line in Shell.run("/bin/launchctl", ["print-disabled", domain]).out.split(separator: "\n") {
                // "com.foo.bar" => disabled   (older: => true)
                let t = line.trimmingCharacters(in: .whitespaces)
                guard t.hasPrefix("\""), let end = t.dropFirst().firstIndex(of: "\"") else { continue }
                let label = String(t[t.index(after: t.startIndex)..<end])
                if t.hasSuffix("disabled") || t.hasSuffix("true") { s.insert(label) }
            }
            return s
        }
        let userOff = disabledSet("gui/\(uid)"), sysOff = disabledSet("system")
        for (dir, dom) in [(H("Library/LaunchAgents"), LaunchItem.Domain.agent), ("/Library/LaunchAgents", .globalAgent), ("/Library/LaunchDaemons", .daemon)] {
            for p in Files.children(dir) where p.hasSuffix(".plist") {
                guard let d = FileManager.default.contents(atPath: p),
                      let pl = try? PropertyListSerialization.propertyList(from: d, options: [], format: nil) as? [String: Any] else { continue }
                let label = pl["Label"] as? String ?? ((p as NSString).lastPathComponent as NSString).deletingPathExtension
                let prog = (pl["Program"] as? String) ?? ((pl["ProgramArguments"] as? [String])?.first ?? "")
                var it = LaunchItem(label: label, path: p, program: prog, domain: dom)
                it.runAtLoad = pl["RunAtLoad"] as? Bool ?? false
                it.keepAlive = (pl["KeepAlive"] as? Bool) ?? (pl["KeepAlive"] != nil)
                it.disabled = dom == .daemon ? sysOff.contains(label) : userOff.contains(label) || (pl["Disabled"] as? Bool ?? false)
                it.loaded = dom == .daemon ? Shell.run("/bin/launchctl", ["print", "system/\(label)"], timeout: 5).status == 0
                                           : loadedLabels.contains(label)
                it.vendorMissing = !prog.isEmpty && prog.hasPrefix("/") && !Files.exists(prog)
                out.append(it)
            }
        }
        // login items (System Events)
        let r = Shell.osascript("tell application \"System Events\" to get (name of every login item) & \"|||\" & (path of every login item)")
        if r.status == 0 {
            let parts = r.out.trimmingCharacters(in: .whitespacesAndNewlines).components(separatedBy: "|||")
            let names = parts.first?.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces).trimmingCharacters(in: CharacterSet(charactersIn: ", ")) } ?? []
            let paths = parts.count > 1 ? parts[1].split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) } : []
            for (i, n) in names.enumerated() where !n.isEmpty {
                let p = i < paths.count ? paths[i] : ""
                out.append(LaunchItem(label: n, path: p, program: p, domain: .login, runAtLoad: true, loaded: true,
                                      vendorMissing: !p.isEmpty && !Files.exists(p)))
            }
        }
        return out.sorted { ($0.domain.rawValue, $0.label.lowercased()) < ($1.domain.rawValue, $1.label.lowercased()) }
    }

    func toggle(_ it: LaunchItem) async {
        busy = true
        let uid = Startup.uid
        let q = Shell.quote
        var res: Shell.Result
        switch it.domain {
        case .agent, .globalAgent:
            if it.disabled {
                res = Shell.sh("/bin/launchctl enable gui/\(uid)/\(q(it.label)); /bin/launchctl bootstrap gui/\(uid) \(q(it.path)) 2>&1; true")
            } else {
                res = Shell.sh("/bin/launchctl disable gui/\(uid)/\(q(it.label)); /bin/launchctl bootout gui/\(uid)/\(q(it.label)) 2>&1; true")
            }
        case .daemon:
            let cmd = it.disabled
                ? "/bin/launchctl enable system/\(q(it.label)); /bin/launchctl bootstrap system \(q(it.path)); true"
                : "/bin/launchctl disable system/\(q(it.label)); /bin/launchctl bootout system/\(q(it.label)); true"
            res = await Task.detached { Shell.admin(cmd, prompt: "putz needs your password to change a system daemon.") }.value
        case .login:
            res = Shell.osascript("tell application \"System Events\" to delete login item \(Uninstaller.asQuote(it.label))")
        }
        log.insert("\(it.disabled ? "enabled" : "disabled") \(it.label)" + (res.out.isEmpty ? "" : " · " + res.out.trimmingCharacters(in: .whitespacesAndNewlines).prefix(160)), at: 0)
        await load()
    }

    /// moves the plist to the bin (after unloading it)
    func remove(_ it: LaunchItem) async {
        busy = true
        let uid = Startup.uid, q = Shell.quote
        switch it.domain {
        case .agent:
            Shell.run("/bin/launchctl", ["bootout", "gui/\(uid)/\(it.label)"])
            if let e = Files.remove(it.path, how: .trash) { log.insert("couldn't remove \(it.label): \(e)", at: 0) }
            else { log.insert("moved \(Fmt.tilde(it.path)) to the bin", at: 0) }
        case .globalAgent, .daemon:
            let dom = it.domain == .daemon ? "system" : "gui/\(uid)"
            let dest = H(".Trash/") + (it.path as NSString).lastPathComponent
            let cmd = "/bin/launchctl bootout \(dom)/\(q(it.label)) 2>/dev/null; /bin/mv -f \(q(it.path)) \(q(dest))"
            let r = await Task.detached { Shell.admin(cmd, prompt: "putz needs your password to remove a system launch item.") }.value
            log.insert(r.status == 0 ? "moved \(it.path) to the bin" : "couldn't remove \(it.label)", at: 0)
        case .login:
            _ = Shell.osascript("tell application \"System Events\" to delete login item \(Uninstaller.asQuote(it.label))")
            log.insert("removed login item \(it.label)", at: 0)
        }
        await load()
    }
}
