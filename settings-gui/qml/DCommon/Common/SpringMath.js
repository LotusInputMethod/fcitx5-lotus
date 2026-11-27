.pragma library

function coefficients(stiffness, damping, mass, seconds) {
    if (!Number.isFinite(seconds) || !Number.isFinite(stiffness) || !Number.isFinite(damping) || !Number.isFinite(mass))
        return [0, 0, 0, 0];
    if (seconds <= 0)
        return [1, 0, 0, 1];

    const inverseMass = 1 / Math.max(0.001, mass);
    const omegaSquared = Math.max(0, stiffness) * inverseMass;
    const decay = Math.max(0, damping) * inverseMass / 2;
    const discriminant = decay * decay - omegaSquared;
    const tolerance = 1e-10 * Math.max(1, omegaSquared);
    if (discriminant > tolerance) {
        const root = Math.sqrt(discriminant);
        const slow = -omegaSquared / (decay + root);
        const fast = -decay - root;
        const slowExp = Math.exp(slow * seconds);
        const fastExp = Math.exp(fast * seconds);
        const divisor = slow - fast;
        const b = (slowExp - fastExp) / divisor;
        return [(-fast * slowExp + slow * fastExp) / divisor, b, -omegaSquared * b, (slow * slowExp - fast * fastExp) / divisor];
    }

    const envelope = Math.exp(-decay * seconds);
    let cosine = 1;
    let sine = seconds;
    if (discriminant < -tolerance) {
        const frequency = Math.sqrt(-discriminant);
        cosine = Math.cos(frequency * seconds);
        sine = Math.sin(frequency * seconds) / frequency;
    }
    const b = envelope * sine;
    return [envelope * cosine + decay * b, b, -omegaSquared * b, envelope * cosine - decay * b];
}
