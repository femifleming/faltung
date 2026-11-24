// Sizes, deletion and the guard rails every delete goes through.
import Foundation
import AppKit

let home = NSHomeDirectory()
func H(_ p: String) -> String { (home as NSString).appendingPathComponent(p) }

// MARK: - settings (persisted in UserDefaults)

final class Prefs: ObservableObject {
    static let shared = Prefs()
    private let d = UserDefaults.standard

    @Published var useTrash: Bool { didSet { d.set(useTrash, forKey: "useTrash") } }
    @Published var tempAgeDays: Int { didSet { d.set(tempAgeDays, forKey: "tempAgeDays") } }
    @Published var logAgeDays: Int { didSet { d.set(logAgeDays, forKey: "logAgeDays") } }
    @Published var downloadsAgeDays: Int { didSet { d.set(downloadsAgeDays, forKey: "downloadsAgeDays") } }
    @Published var skipRunning: Bool { didSet { d.set(skipRunning, forKey: "skipRunning") } }
    @Published var appearance: Int { didSet { d.set(appearance, forKey: "appearance") } }   // 0 auto 1 light 2 dark
    @Published var exclusions: [String] { didSet { d.set(exclusions, forKey: "exclusions") } }
    @Published var includes: [String] { didSet { d.set(includes, forKey: "includes") } }
    @Published var ruleState: [String: Bool] { didSet { d.set(ruleState, forKey: "ruleState") } }

    private init() {
        d.register(defaults: ["useTrash": false, "tempAgeDays": 2, "logAgeDays": 0, "downloadsAgeDays": 14,
                              "skipRunning": true, "appearance": 0])
        useTrash = d.bool(forKey: "useTrash")
        tempAgeDays = d.integer(forKey: "tempAgeDays")
        logAgeDays = d.integer(forKey: "logAgeDays")
        downloadsAgeDays = d.integer(forKey: "downloadsAgeDays")
        skipRunning = d.bool(forKey: "skipRunning")
        appearance = d.integer(forKey: "appearance")
        exclusions = d.stringArray(forKey: "exclusions") ?? []
        includes = d.stringArray(forKey: "includes") ?? []
        ruleState = (d.dictionary(forKey: "ruleState") as? [String: Bool]) ?? [:]
    }
}

// MARK: - history of cleans

struct CleanRecord: Codable, Identifiable {
    var id = UUID()
    var date: Date
    var what: String
    var bytes: Int64
    var items: Int
}

final class History: ObservableObject {
    static let shared = History()
    @Published private(set) var records: [CleanRecord] = []
    private let url: URL = {
        let dir = URL(fileURLWithPath: H("Library/Application Support/putz"))
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("history.json")
    }()
    private init() {
        if let data = try? Data(contentsOf: url), let r = try? JSONDecoder().decode([CleanRecord].self, from: data) { records = r }
    }
    func add(_ what: String, bytes: Int64, items: Int) {
        guard items > 0 else { return }
        records.insert(CleanRecord(date: Date(), what: what, bytes: bytes, items: items), at: 0)
        if records.count > 500 { records.removeLast(records.count - 500) }
        if let data = try? JSONEncoder().encode(records) { try? data.write(to: url) }
    }
    var total: Int64 { records.reduce(0) { $0 + $1.bytes } }
}

// MARK: - guard rails

enum Safety {
    /// never removed as a whole, whatever asks
    static let protectedRoots: Set<String> = {
        var s: Set<String> = ["/", "/System", "/Library", "/Applications", "/Users", "/usr", "/bin", "/sbin", "/etc",
                              "/private", "/private/var", "/private/etc", "/var", "/opt", "/Volumes", "/cores",
                              "/Library/Caches", "/Library/Logs", "/Library/Application Support", "/Library/Preferences",
                              "/Library/LaunchAgents", "/Library/LaunchDaemons"]
        for p in ["", "Library", "Library/Caches", "Library/Logs", "Library/Application Support", "Library/Preferences",
                  "Library/Containers", "Library/Group Containers", "Library/Mobile Documents", "Library/Keychains",
                  "Library/Mail", "Library/Messages", "Library/Photos", "Library/Audio", "Library/Fonts",
                  "Library/LaunchAgents", "Library/Developer", "Library/Saved Application State",
                  "Documents", "Desktop", "Downloads", "Music", "Movies", "Pictures", "Applications", "Public",
                  ".Trash", ".ssh", ".gnupg", ".config", ".cache", ".npm", ".cargo", ".gradle", ".m2"] {
            s.insert(H(p).standardizedPath)
        }
        return s
    }()
    /// never touched even inside a cleaned folder
    static let neverInside: [String] = [
        H("Library/Keychains"), H("Library/Mobile Documents"), H("Library/Mail/V"), H("Library/Messages"),
        H(".ssh"), H(".gnupg"), "/System",
    ].map { $0.standardizedPath }

    static func isExcluded(_ path: String) -> Bool {
        let p = path.standardizedPath
        return Prefs.shared.exclusions.contains { ex in
            let e = ex.standardizedPath
            return p == e || p.hasPrefix(e + "/")
        }
    }

    static func check(_ path: String) -> String? {
        let p = path.standardizedPath
        if p.isEmpty || !p.hasPrefix("/") { return "not an absolute path" }
        if protectedRoots.contains(p) { return "protected folder" }
        if p.hasPrefix("/System/") || p.hasPrefix("/usr/") || p.hasPrefix("/bin/") || p.hasPrefix("/sbin/") { return "system path" }
        for n in neverInside where p == n || p.hasPrefix(n + "/") { return "protected folder" }
        if isExcluded(p) { return "excluded in settings" }
        return nil
    }
}

extension String {
    var standardizedPath: String {
        var p = (self as NSString).standardizingPath
        while p.count > 1 && p.hasSuffix("/") { p.removeLast() }
        return p
    }
}

// MARK: - file helpers

enum Files {
    static let fm = FileManager.default

    static func exists(_ p: String) -> Bool { fm.fileExists(atPath: p) }

    static func isDir(_ p: String) -> Bool {
        var d: ObjCBool = false
        return fm.fileExists(atPath: p, isDirectory: &d) && d.boolValue
    }

    static func isSymlink(_ p: String) -> Bool {
        (try? fm.attributesOfItem(atPath: p)[.type] as? FileAttributeType) == .typeSymbolicLink
    }

    /// children (names → full paths); [] if unreadable
    static func children(_ p: String) -> [String] {
        ((try? fm.contentsOfDirectory(atPath: p)) ?? []).map { (p as NSString).appendingPathComponent($0) }
    }

    /// expand "*" in the last components ("~/Library/Application Support/Firefox/Profiles/*/cache2")
    static func glob(_ pattern: String) -> [String] {
        let comps = pattern.split(separator: "/", omittingEmptySubsequences: true).map(String.init)
        var bases = ["/"]
        for c in comps {
            var next: [String] = []
            for b in bases {
                if c.contains("*") {
                    let rx = "^" + NSRegularExpression.escapedPattern(for: c).replacingOccurrences(of: "\\*", with: ".*") + "$"
                    for name in (try? fm.contentsOfDirectory(atPath: b)) ?? [] where name.range(of: rx, options: .regularExpression) != nil {
                        next.append((b as NSString).appendingPathComponent(name))
                    }
                } else {
                    let n = (b as NSString).appendingPathComponent(c)
                    if fm.fileExists(atPath: n) || isSymlink(n) { next.append(n) }
                }
            }
            bases = next
            if bases.isEmpty { break }
        }
        return bases
    }

    /// allocated size on disk, without following symlinks; packages included
    static func size(_ path: String, cancel: (() -> Bool)? = nil) -> (bytes: Int64, files: Int) {
        let url = URL(fileURLWithPath: path)
        let keys: [URLResourceKey] = [.totalFileAllocatedSizeKey, .fileAllocatedSizeKey, .isRegularFileKey, .isSymbolicLinkKey]
        guard let rv = try? url.resourceValues(forKeys: Set(keys + [.isDirectoryKey])) else { return (0, 0) }
        if rv.isSymbolicLink == true { return (0, 1) }
        if rv.isDirectory != true {
            return (Int64(rv.totalFileAllocatedSize ?? rv.fileAllocatedSize ?? 0), 1)
        }
        var total: Int64 = 0, n = 0
        guard let e = fm.enumerator(at: url, includingPropertiesForKeys: keys, options: [], errorHandler: { _, _ in true }) else { return (0, 0) }
        for case let u as URL in e {
            if n % 512 == 0, cancel?() == true { break }
            guard let v = try? u.resourceValues(forKeys: Set(keys)) else { continue }
            if v.isRegularFile == true {
                total += Int64(v.totalFileAllocatedSize ?? v.fileAllocatedSize ?? 0); n += 1
            } else if v.isSymbolicLink == true { n += 1 }
        }
        return (total, n)
    }

    static func modified(_ p: String) -> Date? {
        (try? fm.attributesOfItem(atPath: p)[.modificationDate]) as? Date
    }

    /// newest modification time inside a folder (so a folder of fresh files is not "old")
    static func newestInside(_ p: String, limit: Int = 4000) -> Date? {
        var newest = modified(p)
        guard isDir(p), let e = fm.enumerator(atPath: p) else { return newest }
        var i = 0
        while let rel = e.nextObject() as? String, i < limit {
            i += 1
            if let a = e.fileAttributes, let m = a[.modificationDate] as? Date, m > (newest ?? .distantPast) { newest = m }
            _ = rel
        }
        return newest
    }

    enum Removal { case erase, trash }

    /// the one place putz removes anything
    @discardableResult
    static func remove(_ path: String, how: Removal) -> String? {
        if let why = Safety.check(path) { return why }
        let url = URL(fileURLWithPath: path)
        do {
            switch how {
            case .trash: try fm.trashItem(at: url, resultingItemURL: nil)
            case .erase: try fm.removeItem(at: url)
            }
            return nil
        } catch {
            // a folder we can't remove whole may still have removable contents
            return (error as NSError).localizedDescription
        }
    }

    static func reveal(_ path: String) {
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)])
    }
}

// MARK: - shell

enum Shell {
    struct Result { var status: Int32; var out: String }

    @discardableResult
    static func run(_ exe: String, _ args: [String], timeout: TimeInterval = 120) -> Result {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: exe)
        p.arguments = args
        let pipe = Pipe()
        p.standardOutput = pipe; p.standardError = pipe
        do { try p.run() } catch { return Result(status: -1, out: error.localizedDescription) }
        var data = Data()
        let q = DispatchQueue(label: "putz.read")
        let done = DispatchSemaphore(value: 0)
        q.async { data = pipe.fileHandleForReading.readDataToEndOfFile(); done.signal() }
        let deadline = Date().addingTimeInterval(timeout)
        while p.isRunning && Date() < deadline { usleep(20_000) }
        if p.isRunning { p.terminate() }
        _ = done.wait(timeout: .now() + 2)
        return Result(status: p.isRunning ? -2 : p.terminationStatus, out: String(decoding: data, as: UTF8.self))
    }

    static func sh(_ cmd: String, timeout: TimeInterval = 120) -> Result { run("/bin/sh", ["-c", cmd], timeout: timeout) }

    /// a shell command run as root through the standard macOS password prompt
    static func admin(_ cmd: String, prompt: String) -> Result {
        let esc = cmd.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        let p = prompt.replacingOccurrences(of: "\"", with: "'")
        return run("/usr/bin/osascript", ["-e", "do shell script \"\(esc)\" with prompt \"\(p)\" with administrator privileges"], timeout: 600)
    }

    static func osascript(_ src: String) -> Result { run("/usr/bin/osascript", ["-e", src]) }

    static func quote(_ s: String) -> String { "'" + s.replacingOccurrences(of: "'", with: "'\\''") + "'" }
}

// MARK: - full disk access

enum Access {
    /// TCC-protected files are unreadable without full disk access
    static var fullDisk: Bool {
        for p in [H("Library/Safari/Bookmarks.plist"), H("Library/Safari/CloudTabs.db"), H("Library/Mail"),
                  "/Library/Application Support/com.apple.TCC/TCC.db"] where Files.exists(p) {
            if FileManager.default.isReadableFile(atPath: p) {
                if Files.isDir(p) { return (try? FileManager.default.contentsOfDirectory(atPath: p)) != nil }
                return (try? FileHandle(forReadingFrom: URL(fileURLWithPath: p)))?.readData(ofLength: 1) != nil
            }
            return false
        }
        return (try? FileManager.default.contentsOfDirectory(atPath: H(".Trash"))) != nil
    }
    static func openSettings() {
        NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles")!)
    }
}

// MARK: - running apps

enum Running {
    static func isRunning(_ bundleIDs: [String]) -> String? {
        for a in NSWorkspace.shared.runningApplications {
            if let id = a.bundleIdentifier, bundleIDs.contains(where: { id == $0 || id.hasPrefix($0 + ".") && $0.count > 8 }) {
                return a.localizedName ?? id
            }
        }
        return nil
    }
}
