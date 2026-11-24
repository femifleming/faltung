// dk4 disk (space analyser + large / old files), dp5 duplicates, mt7 tune (maintenance), hc0 health numbers.
import Foundation
import AppKit
import CryptoKit

// MARK: - disk tree

final class Node: Identifiable {
    let id = UUID()
    let name: String
    let path: String
    let isDir: Bool
    var bytes: Int64 = 0
    var files = 0
    var children: [Node] = []
    weak var parent: Node?
    var modified: Date?
    var accessed: Date?
    init(name: String, path: String, isDir: Bool) { self.name = name; self.path = path; self.isDir = isDir }
}

enum Kind: String, CaseIterable {
    case audio, video, images, archives, apps, code, documents, other
    static func of(_ ext: String) -> Kind {
        switch ext.lowercased() {
        case "wav", "aif", "aiff", "flac", "mp3", "m4a", "ogg", "alac", "caf", "als", "asd", "rx2", "opus": return .audio
        case "mov", "mp4", "m4v", "mkv", "avi", "webm", "prproj", "braw", "r3d", "mxf": return .video
        case "jpg", "jpeg", "png", "gif", "heic", "tif", "tiff", "psd", "raw", "cr2", "nef", "dng", "webp", "svg", "ai", "exr": return .images
        case "zip", "dmg", "pkg", "tar", "gz", "tgz", "bz2", "xz", "7z", "rar", "iso", "xip", "zst": return .archives
        case "app", "dylib", "framework", "vst3", "component", "vst", "clap", "so", "a", "o", "bundle": return .apps
        case "swift", "c", "cpp", "h", "hpp", "m", "mm", "js", "ts", "py", "rs", "go", "java", "json", "html", "css", "rb", "sh", "ipynb", "gguf", "safetensors", "bin", "pt": return .code
        case "pdf", "doc", "docx", "pages", "txt", "md", "rtf", "key", "numbers", "xls", "xlsx", "ppt", "pptx", "epub", "indd": return .documents
        default: return .other
        }
    }
    var hex: UInt32 {
        switch self {
        case .audio: 0xee8e6e; case .video: 0x8593d4; case .images: 0xf6c590; case .archives: 0xc9b53b
        case .apps: 0x8ec3e6; case .code: 0x76a582; case .documents: 0xa9bccf; case .other: 0x9a9a92
        }
    }
}

@MainActor
final class DiskScan: ObservableObject {
    @Published var root: Node? = nil
    @Published var current: Node? = nil
    @Published var busy = false
    @Published var status = "pick a folder and scan"
    @Published var scannedFiles = 0
    @Published var kinds: [(Kind, Int64)] = []
    @Published var largest: [Node] = []
    @Published var oldBig: [Node] = []
    @Published var rootPath = home
    private let flag = Flag()
    func cancel() { flag.set(true) }

    func scan() async {
        guard !busy else { return }
        busy = true; flag.set(false); scannedFiles = 0; status = "scanning \(Fmt.tilde(rootPath))"
        let path = rootPath
        let counter = Counter()
        let ticker = Task { @MainActor in
            while !Task.isCancelled { scannedFiles = counter.value; try? await Task.sleep(nanoseconds: 150_000_000) }
        }
        let flag = self.flag
        let result = await Task.detached(priority: .userInitiated) { () -> (Node, [Kind: Int64], [Node]) in
            var kinds: [Kind: Int64] = [:]
            var big: [Node] = []
            let r = DiskScan.build(path, counter: counter, kinds: &kinds, big: &big, cancel: { flag.value })
            return (r, kinds, big)
        }.value
        ticker.cancel()
        root = result.0; current = result.0
        kinds = Kind.allCases.map { ($0, result.1[$0] ?? 0) }.filter { $0.1 > 0 }.sorted { $0.1 > $1.1 }
        largest = Array(result.2.sorted { $0.bytes > $1.bytes }.prefix(200))
        let yearAgo = Date().addingTimeInterval(-365 * 86400)
        oldBig = Array(result.2.filter { ($0.accessed ?? $0.modified ?? .distantFuture) < yearAgo }.sorted { $0.bytes > $1.bytes }.prefix(200))
        scannedFiles = counter.value
        status = "\(Fmt.bytes(result.0.bytes)) in \(Fmt.count(result.0.files)) files" + (flag.value ? " (stopped)" : "")
        busy = false
    }
    final class Counter: @unchecked Sendable {
        private var v = 0; private let l = NSLock()
        func add(_ n: Int) { l.lock(); v += n; l.unlock() }
        var value: Int { l.lock(); defer { l.unlock() }; return v }
    }

    /// builds the tree; files under 1 mb are summed into their folder, bigger ones get their own node
    nonisolated static func build(_ path: String, counter: Counter, kinds: inout [Kind: Int64], big: inout [Node], cancel: () -> Bool) -> Node {
        let root = Node(name: (path as NSString).lastPathComponent, path: path, isDir: true)
        let keys: Set<URLResourceKey> = [.isDirectoryKey, .isSymbolicLinkKey, .totalFileAllocatedSizeKey, .fileAllocatedSizeKey,
                                         .contentModificationDateKey, .contentAccessDateKey, .isPackageKey]
        var stack: [(Node, URL)] = [(root, URL(fileURLWithPath: path))]
        var tick = 0
        while let (node, url) = stack.popLast() {
            tick += 1
            if tick % 64 == 0 && cancel() { break }
            guard let kids = try? FileManager.default.contentsOfDirectory(at: url, includingPropertiesForKeys: Array(keys), options: []) else { continue }
            var small: Int64 = 0, smallN = 0
            for k in kids {
                guard let v = try? k.resourceValues(forKeys: keys) else { continue }
                if v.isSymbolicLink == true { continue }
                if k.path == "/System/Volumes" || k.path == "/Volumes" || k.path == "/dev" { continue }
                if v.isDirectory == true && v.isPackage != true {
                    let c = Node(name: k.lastPathComponent, path: k.path, isDir: true)
                    c.parent = node; c.modified = v.contentModificationDate
                    node.children.append(c)
                    stack.append((c, k))
                } else {
                    let s: Int64, n: Int
                    if v.isPackage == true { let z = Files.size(k.path); s = z.bytes; n = z.files } else {
                        s = Int64(v.totalFileAllocatedSize ?? v.fileAllocatedSize ?? 0); n = 1
                    }
                    counter.add(n)
                    kinds[Kind.of(k.pathExtension), default: 0] += s
                    if s >= 1_000_000 {
                        let f = Node(name: k.lastPathComponent, path: k.path, isDir: false)
                        f.bytes = s; f.files = n; f.parent = node
                        f.modified = v.contentModificationDate; f.accessed = v.contentAccessDate
                        node.children.append(f)
                        if s >= 50_000_000 { big.append(f) }
                    } else { small += s; smallN += n }
                }
            }
            if smallN > 0 {
                let f = Node(name: "\(Fmt.count(smallN)) small files", path: "", isDir: false)
                f.bytes = small; f.files = smallN; f.parent = node
                node.children.append(f)
            }
        }
        sum(root)
        return root
    }
    @discardableResult nonisolated static func sum(_ n: Node) -> (Int64, Int) {
        if !n.isDir { return (n.bytes, n.files) }
        var b: Int64 = 0, f = 0
        for c in n.children { let (cb, cf) = sum(c); b += cb; f += cf }
        n.bytes = b; n.files = f
        n.children.sort { $0.bytes > $1.bytes }
        return (b, f)
    }

    func trash(_ n: Node) {
        guard !n.path.isEmpty else { return }
        if let e = Files.remove(n.path, how: .trash) { status = "couldn't move \(n.name): \(e)"; return }
        History.shared.add("disk · \(n.name)", bytes: n.bytes, items: n.files)
        var p = n.parent
        p?.children.removeAll { $0 === n }
        while let q = p { q.bytes -= n.bytes; q.files -= n.files; p = q.parent }
        largest.removeAll { $0 === n || $0.path.hasPrefix(n.path + "/") }
        oldBig.removeAll { $0 === n || $0.path.hasPrefix(n.path + "/") }
        status = "moved \(n.name) to the bin · \(Fmt.bytes(n.bytes))"
        objectWillChange.send()
    }
}

// MARK: - duplicates

struct DupFile: Identifiable, Hashable {
    var id: String { path }
    var path: String
    var bytes: Int64
    var modified: Date?
    var picked = false
}
struct DupGroup: Identifiable {
    var id: String
    var files: [DupFile]
    var bytes: Int64 { files.first?.bytes ?? 0 }
    var waste: Int64 { bytes * Int64(max(0, files.count - 1)) }
}

@MainActor
final class Duplicates: ObservableObject {
    @Published var folders: [String] = [H("Downloads"), H("Documents"), H("Desktop")].filter(Files.exists)
    @Published var minMB: Double = 1
    @Published var groups: [DupGroup] = []
    @Published var busy = false
    @Published var status = "finds files with the same contents (sha-256)"
    @Published var stage = ""
    private let flag = Flag()
    func cancel() { flag.set(true) }

    var waste: Int64 { groups.reduce(0) { $0 + $1.waste } }
    var pickedBytes: Int64 { groups.flatMap(\.files).filter(\.picked).reduce(0) { $0 + $1.bytes } }

    func scan() async {
        guard !busy, !folders.isEmpty else { return }
        busy = true; flag.set(false); groups = []
        let flag = self.flag
        let roots = folders, minBytes = Int64(minMB * 1_000_000)
        let stageSink: @Sendable (String) -> Void = { s in Task { @MainActor in self.stage = s } }
        let found = await Task.detached(priority: .userInitiated) { () -> [DupGroup] in
            let cancel = { flag.value }
            // 1 · by size
            var bySize: [Int64: [DupFile]] = [:]
            var inodes: Set<UInt64> = []
            var n = 0
            for r in roots {
                guard let e = FileManager.default.enumerator(at: URL(fileURLWithPath: r),
                    includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey, .contentModificationDateKey, .fileResourceIdentifierKey],
                    options: [.skipsPackageDescendants], errorHandler: { _, _ in true }) else { continue }
                for case let u as URL in e {
                    n += 1
                    if n % 2000 == 0 { stageSink("listing · \(Fmt.count(n)) files"); if cancel() { return [] } }
                    guard let v = try? u.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey, .contentModificationDateKey]),
                          v.isRegularFile == true, let s = v.fileSize, Int64(s) >= minBytes else { continue }
                    // the same file reached twice (hard link / overlapping folders)
                    if let st = try? FileManager.default.attributesOfItem(atPath: u.path), let ino = st[.systemFileNumber] as? UInt64 {
                        if inodes.contains(ino) { continue }; inodes.insert(ino)
                    }
                    bySize[Int64(s), default: []].append(DupFile(path: u.path, bytes: Int64(s), modified: v.contentModificationDate))
                }
            }
            // 2 · first 64 kb, 3 · whole file
            var out: [DupGroup] = []
            let cands = bySize.filter { $0.value.count > 1 }
            var done = 0
            for (_, list) in cands {
                done += 1
                if done % 20 == 0 { stageSink("comparing · \(done) / \(cands.count) sizes"); if cancel() { break } }
                var byHead: [String: [DupFile]] = [:]
                for f in list { if let h = Duplicates.hash(f.path, limit: 65536) { byHead[h, default: []].append(f) } }
                for (_, l2) in byHead where l2.count > 1 {
                    var byFull: [String: [DupFile]] = [:]
                    for f in l2 { if let h = Duplicates.hash(f.path, limit: nil) { byFull[h, default: []].append(f) } }
                    for (h, l3) in byFull where l3.count > 1 {
                        out.append(DupGroup(id: h, files: l3.sorted { ($0.modified ?? .distantPast) < ($1.modified ?? .distantPast) }))
                    }
                }
            }
            return out.sorted { $0.waste > $1.waste }
        }.value
        groups = found
        autoPick(keepOldest: true)
        stage = ""
        status = "\(groups.count) sets of duplicates · \(Fmt.bytes(waste)) could be freed"
        busy = false
    }

    nonisolated static func hash(_ path: String, limit: Int?) -> String? {
        guard let h = FileHandle(forReadingAtPath: path) else { return nil }
        defer { try? h.close() }
        var sha = SHA256(), read = 0
        while true {
            let want = limit.map { min(1 << 20, $0 - read) } ?? (1 << 20)
            if want <= 0 { break }
            guard let d = try? h.read(upToCount: want), !d.isEmpty else { break }
            sha.update(data: d); read += d.count
        }
        return sha.finalize().map { String(format: "%02x", $0) }.joined()
    }

    /// tick every copy but one
    func autoPick(keepOldest: Bool) {
        for g in groups.indices {
            let keep = keepOldest ? 0 : groups[g].files.count - 1
            for i in groups[g].files.indices { groups[g].files[i].picked = i != keep }
        }
    }
    func autoPickKeep(inFolder f: String) {
        for g in groups.indices {
            let k = groups[g].files.firstIndex { $0.path.hasPrefix(f + "/") } ?? 0
            for i in groups[g].files.indices { groups[g].files[i].picked = i != k }
        }
    }

    func trashPicked() async {
        busy = true
        var freed: Int64 = 0, n = 0, failed = 0
        for g in groups.indices {
            // never all copies of one file
            if groups[g].files.allSatisfy(\.picked) { groups[g].files[0].picked = false }
            for f in groups[g].files where f.picked {
                if Files.remove(f.path, how: .trash) == nil { freed += f.bytes; n += 1 } else { failed += 1 }
            }
            groups[g].files.removeAll { $0.picked && !Files.exists($0.path) }
        }
        groups.removeAll { $0.files.count < 2 }
        History.shared.add("duplicates", bytes: freed, items: n)
        status = "moved \(n) copies to the bin · \(Fmt.bytes(freed))" + (failed > 0 ? " · \(failed) failed" : "")
        busy = false
    }
}

// MARK: - maintenance

struct Task1: Identifiable {
    let id: String
    let name: String
    let note: String
    let admin: Bool
    let cmd: String
}

@MainActor
final class Maintenance: ObservableObject {
    @Published var running: String? = nil
    @Published var log: [(Date, String, String)] = []
    @Published var snapshots: [String] = []

    let tasks: [Task1] = [
        Task1(id: "dns", name: "flush dns cache", note: "fixes sites that moved or won't resolve", admin: true,
              cmd: "/usr/bin/dscacheutil -flushcache; /usr/bin/killall -HUP mDNSResponder"),
        Task1(id: "purge", name: "free inactive memory", note: "purges disk caches from ram (purge)", admin: true, cmd: "/usr/sbin/purge"),
        Task1(id: "audio", name: "restart core audio", note: "when audio devices hang or crackle; stops all sound for a second", admin: true,
              cmd: "/usr/bin/killall coreaudiod"),
        Task1(id: "ls", name: "rebuild launch services", note: "fixes duplicate 'open with' entries and wrong default apps", admin: false,
              cmd: "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -r -domain local -domain system -domain user"),
        Task1(id: "spot", name: "reindex spotlight", note: "rebuilds the search index of the startup disk (takes a while in the background)", admin: true,
              cmd: "/usr/bin/mdutil -E /"),
        Task1(id: "ql", name: "reset quick look", note: "reloads quick look plug-ins and thumbnails", admin: false,
              cmd: "/usr/bin/qlmanage -r; /usr/bin/qlmanage -r cache"),
        Task1(id: "font", name: "reset font caches", note: "for garbled or missing fonts; log out afterwards", admin: false,
              cmd: "/usr/bin/atsutil databases -removeUser; /usr/bin/atsutil server -shutdown; /usr/bin/atsutil server -ping"),
        Task1(id: "verify", name: "verify startup disk", note: "checks the file system (read-only)", admin: false,
              cmd: "/usr/sbin/diskutil verifyVolume /"),
        Task1(id: "snap", name: "thin local snapshots", note: "removes time machine's local snapshots (they show as 'purgeable')", admin: true,
              cmd: "/usr/bin/tmutil thinlocalsnapshots / 999999999999 4"),
        Task1(id: "dock", name: "restart dock and finder", note: "for a stuck dock, menu bar or desktop", admin: false,
              cmd: "/usr/bin/killall Dock; /usr/bin/killall Finder; /usr/bin/killall SystemUIServer"),
        Task1(id: "perm", name: "repair home permissions", note: "resets access lists on your home folder (diskutil resetUserPermissions)", admin: true,
              cmd: "/usr/sbin/diskutil resetUserPermissions / \(getuid())"),
        Task1(id: "periodic", name: "rotate system logs", note: "runs newsyslog to compress and rotate logs", admin: true,
              cmd: "/usr/sbin/newsyslog -v"),
    ]

    func run(_ t: Task1) async {
        guard running == nil else { return }
        running = t.id
        let r = await Task.detached { t.admin ? Shell.admin(t.cmd, prompt: "putz needs your password to \(t.name).") : Shell.sh(t.cmd, timeout: 900) }.value
        let out = r.out.trimmingCharacters(in: .whitespacesAndNewlines)
        log.insert((Date(), t.name, r.status == 0 ? (out.isEmpty ? "done" : out) : "failed (\(r.status)) \(out)"), at: 0)
        if t.id == "snap" { await loadSnapshots() }
        running = nil
    }

    func loadSnapshots() async {
        let r = await Task.detached { Shell.run("/usr/bin/tmutil", ["listlocalsnapshots", "/"]) }.value
        snapshots = r.out.split(separator: "\n").map(String.init).filter { $0.contains("com.apple") }
    }
}

// MARK: - health numbers

struct DiskStat { var total: Int64 = 0, free: Int64 = 0, important: Int64 = 0 }
struct MemStat { var total: Int64 = 0, used: Int64 = 0, compressed: Int64 = 0, wired: Int64 = 0 }

enum Stats {
    static func disk() -> DiskStat {
        let u = URL(fileURLWithPath: "/")
        let v = try? u.resourceValues(forKeys: [.volumeTotalCapacityKey, .volumeAvailableCapacityKey, .volumeAvailableCapacityForImportantUsageKey])
        return DiskStat(total: Int64(v?.volumeTotalCapacity ?? 0), free: Int64(v?.volumeAvailableCapacity ?? 0),
                        important: v?.volumeAvailableCapacityForImportantUsage ?? 0)
    }
    static func memory() -> MemStat {
        var s = vm_statistics64(), n = mach_msg_type_number_t(MemoryLayout<vm_statistics64>.size / MemoryLayout<integer_t>.size)
        let r = withUnsafeMutablePointer(to: &s) { $0.withMemoryRebound(to: integer_t.self, capacity: Int(n)) { host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &n) } }
        let page = Int64(vm_kernel_page_size)
        var m = MemStat(total: Int64(ProcessInfo.processInfo.physicalMemory))
        if r == KERN_SUCCESS {
            m.wired = Int64(s.wire_count) * page
            m.compressed = Int64(s.compressor_page_count) * page
            m.used = (Int64(s.active_count) + Int64(s.wire_count) + Int64(s.compressor_page_count)) * page
        }
        return m
    }
    static var uptime: String {
        let t = Int(ProcessInfo.processInfo.systemUptime)
        return t > 86400 ? "\(t / 86400) d \(t % 86400 / 3600) h" : "\(t / 3600) h \(t % 3600 / 60) m"
    }
    static var model: String {
        var size = 0; sysctlbyname("hw.model", nil, &size, nil, 0)
        var b = [CChar](repeating: 0, count: max(1, size)); sysctlbyname("hw.model", &b, &size, nil, 0)
        return String(cString: b)
    }
    static var os: String {
        let v = ProcessInfo.processInfo.operatingSystemVersion
        return "macos \(v.majorVersion).\(v.minorVersion).\(v.patchVersion)"
    }
}

final class Flag: @unchecked Sendable {
    private var v = false; private let l = NSLock()
    func set(_ b: Bool) { l.lock(); v = b; l.unlock() }
    var value: Bool { l.lock(); defer { l.unlock() }; return v }
}
