/**
 * print banner MOTHPROBE with correct alignment & dedicated colors
 */
function printBanner() {
    const MAX_HEIGHT = 5; // Tinggi kanvas total (agar ada ruang atas dan bawah)

    // Color definitions (ANSI)
    const colors = {
        gray: '\x1b[38;2;180;180;180m', // Warna Gray Metalik
        yellow: '\x1b[93m',             // Kuning terang untuk mata robot
        reset: '\x1b[0m'
    };

    // Gradient helper for M (Violet -> Red)
    function getGradientVioletRed(row, totalRows) {
        const ratio = totalRows > 1 ? row / totalRows : 0;
        const r = Math.round(148 + (255 - 148) * ratio);
        const b = Math.round(211 + (0 - 211) * ratio);
        return `\x1b[38;2;${r};0;${b}m`;
    }

    // Gradient helper for O (Cyan -> Dark Blue)
    function getGradientCyanDarkBlue(row, totalRows) {
        const ratio = totalRows > 1 ? row / totalRows : 0;
        const g = Math.round(255 + (0 - 255) * ratio);
        const b = Math.round(255 + (139 - 255) * ratio);
        return `\x1b[38;2;0;${g};${b}m`;
    }

    // ASCII Art parts (M = 4 baris, sisanya = 3 baris)
    const parts = {
        M: ["▄    ▄", "▄▄██▄▄", "█░██░█", "█░[]░█"],
        OTH: ["▄████▄ ██████ ██  ██", "██  ██   ██   ██████", "▀████▀   ██   ██  ██"],
        PR: ["█████▄ █████▄", "██▄▄█▀ ██▄▄██▄", "██     ██   ██"],
        O: [" ▄███▄", "██(+)██", " ▀███▀"],
        BE: ["█████▄ ██████", "██▄▄██ ██▄▄", "██▄▄█▀ ██▄▄▄▄"]
    };

    // Lebar setiap bagian (agar spasi antar huruf pas)
    const widths = { M: 7, OTH: 22, PR: 15, O: 7, BE: 14 };
    
    // Offset untuk rata bawah (agar bagian dengan 3 baris diturunkan)
    const offsets = {
        M: MAX_HEIGHT - parts.M.length,   // 5 - 4 = 1 (sedikit jarak atas)
        OTH: MAX_HEIGHT - parts.OTH.length, // 5 - 3 = 2
        PR: MAX_HEIGHT - parts.PR.length,   // 5 - 3 = 2
        O: MAX_HEIGHT - parts.O.length,     // 5 - 3 = 2
        BE: MAX_HEIGHT - parts.BE.length    // 5 - 3 = 2
    };

    // Helper untuk mengambil karakter pada baris tertentu
    function getRow(partData, row, width, offset) {
        let idx = row - offset;
        let str = '';
        if (idx >= 0 && idx < partData.length) {
            str = partData[idx];
        } else {
            str = ' '.repeat(width);
        }
        return str.padEnd(width, ' ');
    }

    let result = '';
    for (let row = 0; row < MAX_HEIGHT; row++) {
        // 1. M -> Violet ke Merah
        let colorM = getGradientVioletRed(row, MAX_HEIGHT - 1);
        let lineM = colorM + getRow(parts.M, row, widths.M, offsets.M) + colors.reset;

        // 2. OTH -> Gray Metalik
        let lineOTH = colors.gray + getRow(parts.OTH, row, widths.OTH, offsets.OTH) + colors.reset;

        // 3. PR -> Gray Metalik
        let linePR = colors.gray + getRow(parts.PR, row, widths.PR, offsets.PR) + colors.reset;

        // 4. O -> Cyan ke Biru Tua (dengan mata kuning menyala)
        let colorO = getGradientCyanDarkBlue(row, MAX_HEIGHT - 1);
        let lineO = colorO + getRow(parts.O, row, widths.O, offsets.O) + colors.reset;
        lineO = lineO.replace('(+)', colors.yellow + '(+)' + colors.reset); // Sorot mata robot

        // 5. BE -> Gray Metalik
        let lineBE = colors.gray + getRow(parts.BE, row, widths.BE, offsets.BE) + colors.reset;

        // Gabungkan dengan spasi (2 spasi antar blok)
        let combined = lineM + '  ' + lineOTH + '  ' + linePR + '  ' + lineO + '  ' + lineBE;
        result += combined + '\n';
    }

    return result;
}

// Jalankan banner
console.log(printBanner());