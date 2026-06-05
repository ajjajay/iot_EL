// Normalised RMS distance — mirrors BiometricManager::match() on the ESP32.

function rmsDistance(a, b) {
  let sum = 0;
  for (let i = 0; i < 64; i++) sum += (a[i] - b[i]) ** 2;
  return Math.sqrt(sum / 64);
}

/**
 * Match a 64-element query vector against all users in the Firebase /users object.
 * Only users with a `template` array and `active: true` are considered.
 *
 * @param {number[]} query       - 64-element feature vector
 * @param {Object}   users       - Firebase /users snapshot value
 * @param {number}   threshold   - RMS distance cut-off (default 0.30, same as firmware)
 * @returns {{ matched, userId, userName, score }}
 */
export function matchAgainstUsers(query, users, threshold = 0.30) {
  let bestScore = Infinity, bestUser = null;

  for (const user of Object.values(users)) {
    if (!user.active || !Array.isArray(user.template)) continue;
    const score = rmsDistance(query, user.template);
    if (score < bestScore) {
      bestScore = score;
      bestUser  = user;
    }
  }

  if (bestUser && bestScore < threshold) {
    return { matched: true, userId: bestUser.userId, userName: bestUser.name, score: bestScore };
  }
  return { matched: false, userId: null, userName: null, score: bestScore === Infinity ? 1 : bestScore };
}
