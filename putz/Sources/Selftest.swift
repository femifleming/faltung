// putz --analyse / --orphans / --startup / --apps: read-only checks from the terminal; nothing is removed.
import Foundation
import AppKit
import SwiftUI

enum Selftest {
    static func runIfAsked() {
        let a = CommandLine.arguments
        guard a.count > 1, a[1].hasPrefix("--") else { return }
        switch a[1] {
        case "--analyse":
            let rules = Rules.all(), claimed = Rules.claimedCaches(rules)
            var total: Int64 = 0
            for r in rules {
                let items = Cleaner.scan(r, claimed: claimed)
                let b = items.reduce(0) { $0 + $1.bytes }
                total += b
                print(String(format: "%-26@ %-10@ %5d items  %@", r.id as NSString, Fmt.bytes(b) as NSString, items.count,
                             (items.first.map { Fmt.tilde($0.path) } ?? "") as NSString))
                for i in items where Safety.check(i.path) != nil && i.kind == "path" { print("   !! unsafe item listed: \(i.path)") }
            }
            print("total \(Fmt.bytes(total))")
        case "--orphans":
            let installed = AppScan.installedIDs()
            print("installed ids: \(installed.count)")
            let sem = DispatchSemaphore(value: 0)
            Task { @MainActor in
                let o = Orphans(); await o.scan()
                for i in o.items.prefix(60) { print("\(i.place)\t\(i.bundleID)\t\(i.bytes.map(Fmt.bytes) ?? "?")") }
                print("broken:", o.broken.map { $0.bundleID })
                print(o.status); sem.signal()
            }
            while sem.wait(timeout: .now()) == .timedOut { RunLoop.main.run(until: Date().addingTimeInterval(0.05)) }
        case "--startup":
            for i in Startup.collect() { print("\(i.domain.rawValue)\t\(i.label)\tloaded=\(i.loaded) off=\(i.disabled) missing=\(i.vendorMissing)") }
        case "--apps":
            let ps = AppScan.appPaths()
            print("\(ps.count) apps")
            if let p = ps.first(where: { $0.contains(a.count > 2 ? a[2] : "Discord") }), let info = AppScan.info(p) {
                print(info.name, info.bundleID, info.version, Fmt.date(info.lastUsed), "system=\(info.system)")
                for l in AppScan.leftovers(for: info) { print("  \(l.why)\t\(Fmt.tilde(l.path))\t\(Fmt.bytes(l.bytes))\tpicked=\(l.picked)") }
            }
        case "--shots":
            Fonts.register()
            let dir = a.count > 2 ? a[2] : "."
            MainActor.assumeIsolated {
                for pg in Page.allCases {
                    let v = Root(start: pg).frame(width: 1180, height: 760)
                    let r = ImageRenderer(content: v); r.scale = 1
                    if let img = r.nsImage, let t = img.tiffRepresentation, let b = NSBitmapImageRep(data: t),
                       let png = b.representation(using: .png, properties: [:]) {
                        try? png.write(to: URL(fileURLWithPath: "\(dir)/\(pg.code).png"))
                    }
                }
            }
        case "--fixture":
            // deletes only inside the given throwaway folder
            let dir = a[2]
            let r = Rule(id: "t", group: .custom, name: "t", note: "", targets: [.children(dir, age: nil)])
            let found = Cleaner.scan(r, claimed: [])
            print("found", found.count, Fmt.bytes(found.reduce(0) { $0 + $1.bytes }))
            let out = Cleaner.apply(r, found, how: .erase)
            print("freed", Fmt.bytes(out.bytes), "items", out.count, "failed", out.failed)
            print("left:", Files.children(dir))
            print("guard home:", Safety.check(home) ?? "nil", "· guard docs:", Safety.check(H("Documents")) ?? "nil", "· guard /:", Safety.check("/") ?? "nil")
        default: return
        }
        exit(0)
    }
}
