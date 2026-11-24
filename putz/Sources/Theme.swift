// putz — the faltung look in SwiftUI: flat colour fields, lowercase Inter + IBM Plex Mono,
// skewed parallelograms for every bar, wireframe polyhedra.
import SwiftUI
import AppKit
import CoreText

// MARK: - colour

struct HSL { var h, s, l: Double }

extension Color {
    init(hex: UInt32) {
        self.init(.sRGB,
                  red: Double((hex >> 16) & 0xff) / 255,
                  green: Double((hex >> 8) & 0xff) / 255,
                  blue: Double(hex & 0xff) / 255, opacity: 1)
    }
}

struct RGB {
    var r, g, b: Double
    init(hex: UInt32) {
        r = Double((hex >> 16) & 0xff) / 255; g = Double((hex >> 8) & 0xff) / 255; b = Double(hex & 0xff) / 255
    }
    init(r: Double, g: Double, b: Double) { self.r = r; self.g = g; self.b = b }
    var color: Color { Color(.sRGB, red: r, green: g, blue: b, opacity: 1) }
    /// JUCE's perceived brightness
    var brightness: Double { sqrt(r * r * 0.241 + g * g * 0.691 + b * b * 0.068) }

    var hsl: HSL {
        let mx = max(r, g, b), mn = min(r, g, b), l = (mx + mn) / 2
        if mx == mn { return HSL(h: 0, s: 0, l: l) }
        let d = mx - mn
        let s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn)
        var h: Double
        if mx == r { h = (g - b) / d + (g < b ? 6 : 0) } else if mx == g { h = (b - r) / d + 2 } else { h = (r - g) / d + 4 }
        return HSL(h: h / 6, s: s, l: l)
    }
    init(hsl c: HSL) {
        func hue(_ p: Double, _ q: Double, _ t0: Double) -> Double {
            var t = t0; if t < 0 { t += 1 }; if t > 1 { t -= 1 }
            if t < 1 / 6 { return p + (q - p) * 6 * t }
            if t < 1 / 2 { return q }
            if t < 2 / 3 { return p + (q - p) * (2 / 3 - t) * 6 }
            return p
        }
        if c.s == 0 { self.init(r: c.l, g: c.l, b: c.l); return }
        let q = c.l < 0.5 ? c.l * (1 + c.s) : c.l + c.s - c.l * c.s, p = 2 * c.l - q
        self.init(r: hue(p, q, c.h + 1 / 3), g: hue(p, q, c.h), b: hue(p, q, c.h - 1 / 3))
    }

    /// faltung deep: the same hue printed on dark stock (port of fal::ui::darkField)
    var darkField: RGB {
        let c = hsl
        let chroma = c.s * (1 - abs(2 * c.l - 1))
        let l2 = 0.085 + 0.085 * min(1, max(0, chroma * 1.6))
        let s2 = min(0.62, max(0, 0.10 + 0.80 * chroma + (c.l > 0.9 ? 0.12 * c.s : 0)))
        return RGB(hsl: HSL(h: c.h, s: s2, l: l2))
    }
    /// a strip / line colour as it sits on a dark field (port of fal::ui::darkAccent)
    var darkAccent: RGB {
        let c = hsl
        return RGB(hsl: HSL(h: c.h, s: min(1, c.s * 0.94), l: 0.47 + (c.l - 0.47) * 0.62))
    }
}

/// one colour field per page, like one per faltung product
struct Theme {
    var ground: Color, ink: Color, fill: Color, edge: Color, dim: Color, paper: Color
    var dark: Bool

    static func of(_ groundHex: UInt32, _ accentHex: UInt32, dark: Bool) -> Theme {
        let g0 = RGB(hex: groundHex), a0 = RGB(hex: accentHex)
        let g = dark ? g0.darkField : g0
        var a = dark ? a0.darkAccent : a0
        let inkRGB = g.brightness > 0.42 ? RGB(hex: 0x1d1c15) : RGB(hex: 0xf6f6f0)
        if abs(a.brightness - g.brightness) < 0.12 { a = inkRGB }
        let edge = g.brightness > 0.86 ? inkRGB.color : .white
        // the pale panel that lists sit on (faltung tracklist boxes): ground pushed toward white / black
        let paper = dark ? RGB(r: g.r * 0.7, g: g.g * 0.7, b: g.b * 0.7)
                         : RGB(r: g.r + (1 - g.r) * 0.78, g: g.g + (1 - g.g) * 0.78, b: g.b + (1 - g.b) * 0.78)
        return Theme(ground: g.color, ink: inkRGB.color, fill: a.color, edge: edge,
                     dim: inkRGB.color.opacity(0.62), paper: paper.color, dark: dark)
    }
}

private struct ThemeKey: EnvironmentKey { static let defaultValue = Theme.of(0xc9b53b, 0xd8c64e, dark: false) }
extension EnvironmentValues {
    var theme: Theme { get { self[ThemeKey.self] } set { self[ThemeKey.self] = newValue } }
}

// MARK: - type

enum Fonts {
    static func register() {
        guard let dir = Bundle.main.resourceURL?.appendingPathComponent("fonts"),
              let files = try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil) else { return }
        for f in files where f.pathExtension == "ttf" {
            CTFontManagerRegisterFontsForURL(f as CFURL, .process, nil)
        }
    }
}

extension Font {
    static func sans(_ size: CGFloat, medium: Bool = false) -> Font {
        .custom(medium ? "Inter-Medium" : "Inter-Regular", size: size)
    }
    static func mono(_ size: CGFloat, medium: Bool = false) -> Font {
        .custom(medium ? "IBMPlexMono-Medium" : "IBMPlexMono-Regular", size: size)
    }
}

// MARK: - the house shape

struct Skew: Shape {
    var skew: CGFloat = 0.55
    func path(in r: CGRect) -> Path {
        let d = r.height * skew
        var p = Path()
        p.move(to: CGPoint(x: r.minX + d, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX, y: r.minY))
        p.addLine(to: CGPoint(x: r.maxX - d, y: r.maxY))
        p.addLine(to: CGPoint(x: r.minX, y: r.maxY))
        p.closeSubpath()
        return p
    }
}

/// thin rule · slanted fill · white edge — a size or progress bar
struct SkewBar: View {
    var value: Double            // 0…1
    var height: CGFloat = 9
    var fill: Color? = nil
    @Environment(\.theme) var t
    var body: some View {
        GeometryReader { g in
            let w = g.size.width, v = min(1, max(0, value))
            ZStack(alignment: .leading) {
                Rectangle().fill(t.ink.opacity(0.35)).frame(height: 1)
                if v > 0.002 {
                    Skew().fill(fill ?? t.fill).frame(width: max(height, w * v), height: height)
                    Rectangle().fill(t.edge).frame(width: 2, height: height + 2).offset(x: max(0, w * v - 2))
                }
            }
            .frame(height: g.size.height)
        }
        .frame(height: height + 2)
    }
}

/// small parallelogram, filled when on
struct SkewCheck: View {
    @Binding var on: Bool
    var mixed = false
    @Environment(\.theme) var t
    var body: some View {
        ZStack {
            Skew(skew: 0.6).stroke(t.ink.opacity(0.75), lineWidth: 1)
            if on { Skew(skew: 0.6).fill(t.ink) }
            else if mixed { Skew(skew: 0.6).fill(t.ink.opacity(0.4)).padding(3) }
        }
        .frame(width: 18, height: 10)
        .contentShape(Rectangle())
        .onTapGesture { on.toggle() }
    }
}

/// faltung TextLink: mono text in a skewed outline, filled when primary / lit
struct FLink: View {
    var text: String
    var primary = false
    var disabled = false
    var action: () -> Void
    @Environment(\.theme) var t
    @State private var hover = false
    var body: some View {
        Text(text)
            .font(.mono(11, medium: primary))
            .foregroundStyle(primary ? t.ground : t.ink)
            .padding(.horizontal, 14).padding(.vertical, 5)
            .background(
                ZStack {
                    if primary { Skew(skew: 0.5).fill(t.ink.opacity(hover ? 0.85 : 1)) }
                    else { Skew(skew: 0.5).stroke(t.ink.opacity(hover ? 1 : 0.55), lineWidth: 1) }
                })
            .opacity(disabled ? 0.35 : 1)
            .contentShape(Rectangle())
            .onHover { hover = $0 }
            .onTapGesture { if !disabled { action() } }
    }
}

/// "label  value" tabs, like ChoiceBox asTabs
struct Tabs<T: Hashable>: View {
    var items: [(T, String)]
    @Binding var selection: T
    @Environment(\.theme) var t
    var body: some View {
        HStack(spacing: 2) {
            ForEach(items, id: \.0) { item in
                let on = item.0 == selection
                Text(item.1).font(.mono(11))
                    .foregroundStyle(on ? t.ground : t.ink)
                    .padding(.horizontal, 12).padding(.vertical, 4)
                    .background(Skew(skew: 0.5).fill(on ? t.ink : t.ink.opacity(0.08)))
                    .contentShape(Rectangle())
                    .onTapGesture { selection = item.0 }
            }
        }
    }
}

/// mono label on a hairline, the faltung section rule
struct Hairline: View {
    var label: String
    var trailing: String = ""
    @Environment(\.theme) var t
    var body: some View {
        HStack(spacing: 8) {
            Text(label).font(.mono(10)).foregroundStyle(t.dim)
            Rectangle().fill(t.ink.opacity(0.25)).frame(height: 1)
            if !trailing.isEmpty { Text(trailing).font(.mono(10)).foregroundStyle(t.dim) }
        }
    }
}

/// skewed chip of colour (legend / category swatch)
struct Chip: View {
    var color: Color
    var body: some View { Skew(skew: 0.6).fill(color).frame(width: 14, height: 9) }
}

// MARK: - wireframe polyhedron (Mark Fell sleeves): thin lines, one white highlight face

struct Polyhedron: View {
    var spinning: Bool
    var kind: Int = 0            // 0 icosahedron · 1 octahedron · 2 cube · 3 tetra
    @Environment(\.theme) var t

    var body: some View {
        TimelineView(.animation(minimumInterval: 1 / 30, paused: !spinning)) { tl in
            Canvas { ctx, size in
                let time = spinning ? tl.date.timeIntervalSinceReferenceDate : 0.6
                let (v, faces) = Polyhedron.mesh(kind)
                let ay = time * 0.45, ax = 0.42 + sin(time * 0.21) * 0.25
                let s = min(size.width, size.height) * 0.42
                let c = CGPoint(x: size.width / 2, y: size.height / 2)
                func proj(_ p: (Double, Double, Double)) -> (CGPoint, Double) {
                    var (x, y, z) = p
                    (x, z) = (x * cos(ay) + z * sin(ay), -x * sin(ay) + z * cos(ay))
                    (y, z) = (y * cos(ax) - z * sin(ax), y * sin(ax) + z * cos(ax))
                    let f = 3.2 / (3.2 + z)
                    return (CGPoint(x: c.x + x * s * f, y: c.y + y * s * f), z)
                }
                let pts = v.map(proj)
                // highlight: the face nearest the viewer
                var best = 0, bestZ = Double.infinity
                for (i, f) in faces.enumerated() {
                    let z = f.map { pts[$0].1 }.reduce(0, +) / Double(f.count)
                    if z < bestZ { bestZ = z; best = i }
                }
                var hl = Path()
                for (k, i) in faces[best].enumerated() { k == 0 ? hl.move(to: pts[i].0) : hl.addLine(to: pts[i].0) }
                hl.closeSubpath()
                ctx.fill(hl, with: .color(t.edge.opacity(0.85)))
                var lines = Path()
                for f in faces {
                    for k in 0..<f.count {
                        lines.move(to: pts[f[k]].0); lines.addLine(to: pts[f[(k + 1) % f.count]].0)
                    }
                }
                ctx.stroke(lines, with: .color(t.ink.opacity(0.7)), lineWidth: 0.8)
            }
        }
    }

    static func mesh(_ kind: Int) -> ([(Double, Double, Double)], [[Int]]) {
        switch kind {
        case 1:
            return ([(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)],
                    [[0, 2, 4], [2, 1, 4], [1, 3, 4], [3, 0, 4], [2, 0, 5], [1, 2, 5], [3, 1, 5], [0, 3, 5]])
        case 2:
            let a = 0.62
            return ([(-a, -a, -a), (a, -a, -a), (a, a, -a), (-a, a, -a), (-a, -a, a), (a, -a, a), (a, a, a), (-a, a, a)],
                    [[0, 1, 2, 3], [4, 5, 6, 7], [0, 1, 5, 4], [2, 3, 7, 6], [1, 2, 6, 5], [0, 3, 7, 4]])
        case 3:
            let a = 0.75
            return ([(a, a, a), (-a, -a, a), (-a, a, -a), (a, -a, -a)], [[0, 1, 2], [0, 3, 1], [0, 2, 3], [1, 3, 2]])
        default:
            let p = (1 + sqrt(5.0)) / 2, n = sqrt(1 + p * p)
            let v: [(Double, Double, Double)] = [(-1, p, 0), (1, p, 0), (-1, -p, 0), (1, -p, 0), (0, -1, p), (0, 1, p),
                                                 (0, -1, -p), (0, 1, -p), (p, 0, -1), (p, 0, 1), (-p, 0, -1), (-p, 0, 1)]
                .map { ($0.0 / n, $0.1 / n, $0.2 / n) }
            let f = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6],
                     [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]]
            return (v, f)
        }
    }
}

/// pale chevron strips (stdio sleeve) drawn behind a page header
struct Chevrons: View {
    @Environment(\.theme) var t
    var body: some View {
        Canvas { ctx, size in
            for i in 0..<6 {
                let x0 = size.width * 0.35 + CGFloat(i) * 70, h = size.height
                var p = Path()
                p.move(to: CGPoint(x: x0, y: h)); p.addLine(to: CGPoint(x: x0 + 50, y: h))
                p.addLine(to: CGPoint(x: x0 + 50 + h * 0.5, y: h * 0.5)); p.addLine(to: CGPoint(x: x0 + 50, y: 0))
                p.addLine(to: CGPoint(x: x0, y: 0)); p.addLine(to: CGPoint(x: x0 + h * 0.5, y: h * 0.5)); p.closeSubpath()
                ctx.fill(p, with: .color(t.fill.opacity(i % 2 == 0 ? 0.16 : 0.24)))
            }
        }
        .allowsHitTesting(false)
    }
}

// MARK: - formatting

enum Fmt {
    static func bytes(_ b: Int64) -> String {
        if b <= 0 { return "0 b" }
        let u = ["b", "kb", "mb", "gb", "tb"]
        var v = Double(b), i = 0
        while v >= 1000 && i < u.count - 1 { v /= 1000; i += 1 }
        return i == 0 ? "\(b) b" : String(format: v >= 100 ? "%.0f %@" : (v >= 10 ? "%.1f %@" : "%.2f %@"), v, u[i])
    }
    static func count(_ n: Int) -> String {
        let f = NumberFormatter(); f.numberStyle = .decimal; f.groupingSeparator = " "
        return f.string(from: NSNumber(value: n)) ?? "\(n)"
    }
    static func date(_ d: Date?) -> String {
        guard let d else { return "—" }
        let f = DateFormatter(); f.dateFormat = "yyyy-MM-dd"
        return f.string(from: d)
    }
    static func ago(_ d: Date?) -> String {
        guard let d else { return "never" }
        let days = Int(-d.timeIntervalSinceNow / 86400)
        if days < 1 { return "today" }
        if days < 60 { return "\(days) d ago" }
        if days < 730 { return "\(days / 30) mo ago" }
        return "\(days / 365) y ago"
    }
    static func tilde(_ p: String) -> String {
        let h = NSHomeDirectory()
        return p.hasPrefix(h) ? "~" + p.dropFirst(h.count) : p
    }
}
