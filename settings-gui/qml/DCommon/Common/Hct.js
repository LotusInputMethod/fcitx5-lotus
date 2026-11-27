.pragma library

// Ported from Material Color Utilities, Copyright 2021 Google LLC, Apache-2.0:
// https://github.com/material-foundation/material-color-utilities/tree/main/typescript
// Subset: HctSolver, the Cam16 forward transform reduced to hue and chroma, and
// Contrast.darker/lighter. ViewingConditions.DEFAULT is precomputed to constants.
// Numerics are verbatim; conformance against upstream is asserted in tests/hct.test.mjs.

var VC = {
    n: 0.18418651851244416,
    aw: 29.98099719444734,
    nbb: 1.0169191804458757,
    ncb: 1.0169191804458757,
    c: 0.69,
    nc: 1,
    rgbD: [1.02117770275752, 0.9863077294280124, 0.9339605082802299],
    fl: 0.3884814537800353,
    fLRoot: 0.7894826179304937,
    z: 1.909169568483652
};

var SCALED_DISCOUNT_FROM_LINRGB = [[0.001200833568784504, 0.002389694492170889, 0.0002795742885861124], [0.0005891086651375999, 0.0029785502573438758, 0.0003270666104008398], [0.00010146692491640572, 0.0005364214359186694, 0.0032979401770712076]];
var LINRGB_FROM_SCALED_DISCOUNT = [[1373.2198709594231, -1100.4251190754821, -7.278681089101213], [-271.815969077903, 559.6580465940733, -32.46047482791194], [1.9622899599665666, -57.173814538844006, 308.7233197812385]];
var Y_FROM_LINRGB = [0.2126, 0.7152, 0.0722];
var CRITICAL_PLANES = [0.015176349177441876, 0.045529047532325624, 0.07588174588720938, 0.10623444424209313, 0.13658714259697685, 0.16693984095186062, 0.19729253930674434, 0.2276452376616281, 0.2579979360165119, 0.28835063437139563, 0.3188300904430532, 0.350925934958123, 0.3848314933096426, 0.42057480301049466, 0.458183274052838, 0.4976837250274023, 0.5391024159806381, 0.5824650784040898, 0.6277969426914107, 0.6751227633498623, 0.7244668422128921, 0.775853049866786, 0.829304845476233, 0.8848452951698498, 0.942497089126609, 1.0022825574869039, 1.0642236851973577, 1.1283421258858297, 1.1946592148522128, 1.2631959812511864, 1.3339731595349034, 1.407011200216447, 1.4823302800086415, 1.5599503113873272, 1.6398909516233677, 1.7221716113234105, 1.8068114625156377, 1.8938294463134073, 1.9832442801866852, 2.075074464868551, 2.1693382909216234, 2.2660538449872063, 2.36523901573795, 2.4669114995532007, 2.5710888059345764, 2.6777882626779785, 2.7870270208169257, 2.898822059350997, 3.0131901897720907, 3.1301480604002863, 3.2497121605402226, 3.3718988244681087, 3.4967242352587946, 3.624204428461639, 3.754355295633311, 3.887192587735158, 4.022731918402185, 4.160988767090289, 4.301978482107941, 4.445716283538092, 4.592217266055746, 4.741496401646282, 4.893568542229298, 5.048448422192488, 5.20615066083972, 5.3666897647573375, 5.5300801301023865, 5.696336044816294, 5.865471690767354, 6.037501145825082, 6.212438385869475, 6.390297286737924, 6.571091626112461, 6.7548350853498045, 6.941541251256611, 7.131223617812143, 7.323895587840543, 7.5195704746346665, 7.7182615035334345, 7.919981813454504, 8.124744458384042, 8.332562408825165, 8.543448553206703, 8.757415699253682, 8.974476575321063, 9.194643831691977, 9.417930041841839, 9.644347703669503, 9.873909240696694, 10.106627003236781, 10.342513269534024, 10.58158024687427, 10.8238400726681, 11.069304815507364, 11.317986476196008, 11.569896988756009, 11.825048221409341, 12.083451977536606, 12.345119996613247, 12.610063955123938, 12.878295467455942, 13.149826086772048, 13.42466730586372, 13.702830557985108, 13.984327217668513, 14.269168601521828, 14.55736596900856, 14.848930523210871, 15.143873411576273, 15.44220572664832, 15.743938506781891, 16.04908273684337, 16.35764934889634, 16.66964922287304, 16.985093187232053, 17.30399201960269, 17.62635644741625, 17.95219714852476, 18.281524751807332, 18.614349837764564, 18.95068293910138, 19.290534541298456, 19.633915083172692, 19.98083495742689, 20.331304511189067, 20.685334046541502, 21.042933821039977, 21.404114048223256, 21.76888489811322, 22.137256497705877, 22.50923893145328, 22.884842241736916, 23.264076429332462, 23.6469514538663, 24.033477234264016, 24.42366364919083, 24.817520537484558, 25.21505769858089, 25.61628489293138, 26.021211842414342, 26.429848230738664, 26.842203703840827, 27.258287870275353, 27.678110301598522, 28.10168053274597, 28.529008062403893, 28.96010235337422, 29.39497283293396, 29.83362889318845, 30.276079891419332, 30.722335150426627, 31.172403958865512, 31.62629557157785, 32.08401920991837, 32.54558406207592, 33.010999283389665, 33.4802739966603, 33.953417292456834, 34.430438229418264, 34.911345834551085, 35.39614910352207, 35.88485700094671, 36.37747846067349, 36.87402238606382, 37.37449765026789, 37.87891309649659, 38.38727753828926, 38.89959975977785, 39.41588851594697, 39.93615253289054, 40.460400508064545, 40.98864111053629, 41.520882981230194, 42.05713473317016, 42.597404951718396, 43.141702194811224, 43.6900349931913, 44.24241185063697, 44.798841244188324, 45.35933162437017, 45.92389141541209, 46.49252901546552, 47.065252796817916, 47.64207110610409, 48.22299226451468, 48.808024568002054, 49.3971762874833, 49.9904556690408, 50.587870934119984, 51.189430279724725, 51.79514187861014, 52.40501387947288, 53.0190544071392, 53.637271562750364, 54.259673423945976, 54.88626804504493, 55.517063457223934, 56.15206766869424, 56.79128866487574, 57.43473440856916, 58.08241284012621, 58.734331877617365, 59.39049941699807, 60.05092333227251, 60.715611475655585, 61.38457167773311, 62.057811747619894, 62.7353394731159, 63.417162620860914, 64.10328893648692, 64.79372614476921, 65.48848194977529, 66.18756403501224, 66.89098006357258, 67.59873767827808, 68.31084450182222, 69.02730813691093, 69.74813616640164, 70.47333615344107, 71.20291564160104, 71.93688215501312, 72.67524319850172, 73.41800625771542, 74.16517879925733, 74.9167682708136, 75.67278210128072, 76.43322770089146, 77.1981124613393, 77.96744375590167, 78.74122893956174, 79.51947534912904, 80.30219030335869, 81.08938110306934, 81.88105503125999, 82.67721935322541, 83.4778813166706, 84.28304815182372, 85.09272707154808, 85.90692527145302, 86.72564993000343, 87.54890820862819, 88.3767072518277, 89.2090541872801, 90.04595612594655, 90.88742016217518, 91.73345337380438, 92.58406282226491, 93.43925555268066, 94.29903859396902, 95.16341895893969, 96.03240364439274, 96.9059996312159, 97.78421388448044, 98.6670533535366, 99.55452497210776];

function signum(value) {
    if (value < 0)
        return -1;
    if (value === 0)
        return 0;
    return 1;
}

function sanitizeDegrees(degrees) {
    const wrapped = degrees % 360;
    return wrapped < 0 ? wrapped + 360 : wrapped;
}

function rotationDirection(from, to) {
    return sanitizeDegrees(to - from) <= 180 ? 1 : -1;
}

function differenceDegrees(a, b) {
    return 180 - Math.abs(Math.abs(a - b) - 180);
}

function matrixMultiply(row, matrix) {
    return [row[0] * matrix[0][0] + row[1] * matrix[0][1] + row[2] * matrix[0][2], row[0] * matrix[1][0] + row[1] * matrix[1][1] + row[2] * matrix[1][2], row[0] * matrix[2][0] + row[1] * matrix[2][1] + row[2] * matrix[2][2]];
}

function labF(t) {
    return t > 216 / 24389 ? Math.cbrt(t) : (24389 / 27 * t + 16) / 116;
}

function labInvf(ft) {
    const cubed = ft * ft * ft;
    return cubed > 216 / 24389 ? cubed : (116 * ft - 16) / (24389 / 27);
}

function yFromTone(tone) {
    return 100 * labInvf((tone + 16) / 116);
}

function toneFromY(y) {
    return labF(y / 100) * 116 - 16;
}

function linearized(channel) {
    const normalized = channel / 255;
    if (normalized <= 0.040449936)
        return normalized / 12.92 * 100;
    return Math.pow((normalized + 0.055) / 1.055, 2.4) * 100;
}

function delinearized(channel) {
    const normalized = channel / 100;
    const value = normalized <= 0.0031308 ? normalized * 12.92 : 1.055 * Math.pow(normalized, 1 / 2.4) - 0.055;
    return Math.min(255, Math.max(0, Math.round(value * 255)));
}

function trueDelinearized(channel) {
    const normalized = channel / 100;
    const value = normalized <= 0.0031308 ? normalized * 12.92 : 1.055 * Math.pow(normalized, 1 / 2.4) - 0.055;
    return value * 255;
}

function colorFromLinrgb(linrgb) {
    return Qt.rgba(delinearized(linrgb[0]) / 255, delinearized(linrgb[1]) / 255, delinearized(linrgb[2]) / 255, 1);
}

function colorFromTone(tone) {
    const component = delinearized(yFromTone(tone)) / 255;
    return Qt.rgba(component, component, component, 1);
}

function chromaticAdaptation(component) {
    const af = Math.pow(Math.abs(component), 0.42);
    return signum(component) * 400 * af / (af + 27.13);
}

function inverseChromaticAdaptation(adapted) {
    const adaptedAbs = Math.abs(adapted);
    const base = Math.max(0, 27.13 * adaptedAbs / (400 - adaptedAbs));
    return signum(adapted) * Math.pow(base, 1 / 0.42);
}

function hueOf(linrgb) {
    const scaledDiscount = matrixMultiply(linrgb, SCALED_DISCOUNT_FROM_LINRGB);
    const rA = chromaticAdaptation(scaledDiscount[0]);
    const gA = chromaticAdaptation(scaledDiscount[1]);
    const bA = chromaticAdaptation(scaledDiscount[2]);
    return Math.atan2((rA + gA - 2 * bA) / 9, (11 * rA + -12 * gA + bA) / 11);
}

function sanitizeRadians(angle) {
    return (angle + Math.PI * 8) % (Math.PI * 2);
}

function areInCyclicOrder(a, b, c) {
    return sanitizeRadians(b - a) < sanitizeRadians(c - a);
}

function lerpPoint(source, t, target) {
    return [source[0] + (target[0] - source[0]) * t, source[1] + (target[1] - source[1]) * t, source[2] + (target[2] - source[2]) * t];
}

function setCoordinate(source, coordinate, target, axis) {
    const t = (coordinate - source[axis]) / (target[axis] - source[axis]);
    return lerpPoint(source, t, target);
}

function isBounded(x) {
    return x >= 0 && x <= 100;
}

function nthVertex(y, n) {
    const kR = Y_FROM_LINRGB[0];
    const kG = Y_FROM_LINRGB[1];
    const kB = Y_FROM_LINRGB[2];
    const coordA = n % 4 <= 1 ? 0 : 100;
    const coordB = n % 2 === 0 ? 0 : 100;
    if (n < 4) {
        const r = (y - coordA * kG - coordB * kB) / kR;
        return isBounded(r) ? [r, coordA, coordB] : [-1, -1, -1];
    }
    if (n < 8) {
        const g = (y - coordB * kR - coordA * kB) / kG;
        return isBounded(g) ? [coordB, g, coordA] : [-1, -1, -1];
    }
    const b = (y - coordA * kR - coordB * kG) / kB;
    return isBounded(b) ? [coordA, coordB, b] : [-1, -1, -1];
}

function bisectToSegment(y, targetHue) {
    let left = [-1, -1, -1];
    let right = left;
    let leftHue = 0;
    let rightHue = 0;
    let initialized = false;
    let uncut = true;
    for (let n = 0; n < 12; n++) {
        const mid = nthVertex(y, n);
        if (mid[0] < 0)
            continue;
        const midHue = hueOf(mid);
        if (!initialized) {
            left = mid;
            right = mid;
            leftHue = midHue;
            rightHue = midHue;
            initialized = true;
            continue;
        }
        if (!uncut && !areInCyclicOrder(leftHue, midHue, rightHue))
            continue;
        uncut = false;
        if (areInCyclicOrder(leftHue, targetHue, midHue)) {
            right = mid;
            rightHue = midHue;
            continue;
        }
        left = mid;
        leftHue = midHue;
    }
    return [left, right];
}

function bisectToLimit(y, targetHue) {
    const segment = bisectToSegment(y, targetHue);
    let left = segment[0];
    let leftHue = hueOf(left);
    let right = segment[1];
    for (let axis = 0; axis < 3; axis++) {
        if (left[axis] === right[axis])
            continue;
        let lPlane = -1;
        let rPlane = 255;
        if (left[axis] < right[axis]) {
            lPlane = Math.floor(trueDelinearized(left[axis]) - 0.5);
            rPlane = Math.ceil(trueDelinearized(right[axis]) - 0.5);
        } else {
            lPlane = Math.ceil(trueDelinearized(left[axis]) - 0.5);
            rPlane = Math.floor(trueDelinearized(right[axis]) - 0.5);
        }
        for (let i = 0; i < 8; i++) {
            if (Math.abs(rPlane - lPlane) <= 1)
                break;
            const mPlane = Math.floor((lPlane + rPlane) / 2);
            const mid = setCoordinate(left, CRITICAL_PLANES[mPlane], right, axis);
            const midHue = hueOf(mid);
            if (areInCyclicOrder(leftHue, targetHue, midHue)) {
                right = mid;
                rPlane = mPlane;
                continue;
            }
            left = mid;
            leftHue = midHue;
            lPlane = mPlane;
        }
    }
    return [(left[0] + right[0]) / 2, (left[1] + right[1]) / 2, (left[2] + right[2]) / 2];
}

function findResultByJ(hueRadians, chroma, y) {
    let j = Math.sqrt(y) * 11;
    const tInnerCoeff = 1 / Math.pow(1.64 - Math.pow(0.29, VC.n), 0.73);
    const eHue = 0.25 * (Math.cos(hueRadians + 2) + 3.8);
    const p1 = eHue * (50000 / 13) * VC.nc * VC.ncb;
    const hSin = Math.sin(hueRadians);
    const hCos = Math.cos(hueRadians);
    for (let round = 0; round < 5; round++) {
        const jNormalized = j / 100;
        const alpha = chroma === 0 || j === 0 ? 0 : chroma / Math.sqrt(jNormalized);
        const t = Math.pow(alpha * tInnerCoeff, 1 / 0.9);
        const ac = VC.aw * Math.pow(jNormalized, 1 / VC.c / VC.z);
        const p2 = ac / VC.nbb;
        const gamma = 23 * (p2 + 0.305) * t / (23 * p1 + 11 * t * hCos + 108 * t * hSin);
        const a = gamma * hCos;
        const b = gamma * hSin;
        const rA = (460 * p2 + 451 * a + 288 * b) / 1403;
        const gA = (460 * p2 - 891 * a - 261 * b) / 1403;
        const bA = (460 * p2 - 220 * a - 6300 * b) / 1403;
        const linrgb = matrixMultiply([inverseChromaticAdaptation(rA), inverseChromaticAdaptation(gA), inverseChromaticAdaptation(bA)], LINRGB_FROM_SCALED_DISCOUNT);
        if (linrgb[0] < 0 || linrgb[1] < 0 || linrgb[2] < 0)
            return null;
        const fnj = Y_FROM_LINRGB[0] * linrgb[0] + Y_FROM_LINRGB[1] * linrgb[1] + Y_FROM_LINRGB[2] * linrgb[2];
        if (fnj <= 0)
            return null;
        if (round === 4 || Math.abs(fnj - y) < 0.002) {
            if (linrgb[0] > 100.01 || linrgb[1] > 100.01 || linrgb[2] > 100.01)
                return null;
            return colorFromLinrgb(linrgb);
        }
        j = j - (fnj - y) * j / (2 * fnj);
    }
    return null;
}

function fromHct(hue, chroma, tone) {
    if (chroma < 0.0001 || tone < 0.0001 || tone > 99.9999)
        return colorFromTone(tone);
    const hueRadians = sanitizeDegrees(hue) / 180 * Math.PI;
    const y = yFromTone(tone);
    const exact = findResultByJ(hueRadians, chroma, y);
    if (exact)
        return exact;
    return colorFromLinrgb(bisectToLimit(y, hueRadians));
}

function toHct(color) {
    const redL = linearized(color.r * 255);
    const greenL = linearized(color.g * 255);
    const blueL = linearized(color.b * 255);
    const x = 0.41233895 * redL + 0.35762064 * greenL + 0.18051042 * blueL;
    const y = 0.2126 * redL + 0.7152 * greenL + 0.0722 * blueL;
    const z = 0.01932141 * redL + 0.11916382 * greenL + 0.95034478 * blueL;
    const rD = VC.rgbD[0] * (0.401288 * x + 0.650173 * y - 0.051461 * z);
    const gD = VC.rgbD[1] * (-0.250268 * x + 1.204414 * y + 0.045854 * z);
    const bD = VC.rgbD[2] * (-0.002079 * x + 0.048952 * y + 0.953127 * z);
    const rAF = Math.pow(VC.fl * Math.abs(rD) / 100, 0.42);
    const gAF = Math.pow(VC.fl * Math.abs(gD) / 100, 0.42);
    const bAF = Math.pow(VC.fl * Math.abs(bD) / 100, 0.42);
    const rA = signum(rD) * 400 * rAF / (rAF + 27.13);
    const gA = signum(gD) * 400 * gAF / (gAF + 27.13);
    const bA = signum(bD) * 400 * bAF / (bAF + 27.13);
    const a = (11 * rA + -12 * gA + bA) / 11;
    const b = (rA + gA - 2 * bA) / 9;
    const u = (20 * rA + 20 * gA + 21 * bA) / 20;
    const p2 = (40 * rA + 20 * gA + bA) / 20;
    const hue = sanitizeDegrees(Math.atan2(b, a) * 180 / Math.PI);
    const j = 100 * Math.pow(p2 * VC.nbb / VC.aw, VC.c * VC.z);
    const huePrime = hue < 20.14 ? hue + 360 : hue;
    const eHue = 0.25 * (Math.cos(huePrime * Math.PI / 180 + 2) + 3.8);
    const p1 = (50000 / 13) * eHue * VC.nc * VC.ncb;
    const t = p1 * Math.sqrt(a * a + b * b) / (u + 0.305);
    const alpha = Math.pow(t, 0.9) * Math.pow(1.64 - Math.pow(0.29, VC.n), 0.73);
    return {
        hue,
        chroma: alpha * Math.sqrt(j / 100),
        tone: 116 * labF(y / 100) - 16
    };
}

function ratioOfYs(y1, y2) {
    const lighter = y1 > y2 ? y1 : y2;
    const darker = lighter === y2 ? y1 : y2;
    return (lighter + 5) / (darker + 5);
}

function darkerTone(tone, ratio) {
    if (tone < 0 || tone > 100)
        return -1;
    const lightY = yFromTone(tone);
    const darkY = (lightY + 5) / ratio - 5;
    if (ratioOfYs(lightY, darkY) < ratio && Math.abs(ratioOfYs(lightY, darkY) - ratio) > 0.04)
        return -1;
    const result = toneFromY(darkY) - 0.4;
    return result < 0 || result > 100 ? -1 : result;
}

function lighterTone(tone, ratio) {
    if (tone < 0 || tone > 100)
        return -1;
    const darkY = yFromTone(tone);
    const lightY = ratio * (darkY + 5) - 5;
    if (ratioOfYs(lightY, darkY) < ratio && Math.abs(ratioOfYs(lightY, darkY) - ratio) > 0.04)
        return -1;
    const result = toneFromY(lightY) + 0.4;
    return result < 0 || result > 100 ? -1 : result;
}

function harmonize(hue, keyHue) {
    const rotation = Math.min(differenceDegrees(hue, keyHue) * 0.5, 15);
    return sanitizeDegrees(hue + rotation * rotationDirection(hue, keyHue));
}
