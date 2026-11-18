import test from 'node:test';
import assert from 'node:assert/strict';
import { currentLocation } from '../dist/location.mjs';

test('location uses one fresh browser fix with a bounded timeout and returns usable coordinates', async () => {
  let calls = 0;
  const location = await currentLocation({secureContext: true, geolocation: {
    getCurrentPosition(success, failure, options) {
      calls++;
      assert.deepEqual(options, {enableHighAccuracy: false, maximumAge: 0, timeout: 12000});
      success({coords: {latitude: 40.42612345, longitude: -86.90912345, accuracy: 35}});
    },
  }});
  assert.equal(calls, 1);
  assert.deepEqual(location, {latitude: 40.426123, longitude: -86.909123, accuracy: 35});
});

test('insecure pages and unsupported browsers keep manual entry available without requesting location', async () => {
  const geolocation = {getCurrentPosition() {throw new Error('must not request');}};
  await assert.rejects(currentLocation({secureContext: false, geolocation}), /HTTPS or localhost.*manually/);
  await assert.rejects(currentLocation({secureContext: true, geolocation: {}}), /cannot provide location.*manually/);
});

test('denied, unavailable and timed out fixes explain how to recover', async () => {
  for (const [code, message] of [[1, /permission denied/], [2, /unavailable/], [3, /timed out/], [0, /Could not get/]]) {
    await assert.rejects(currentLocation({secureContext: true, geolocation: {
      getCurrentPosition(success, failure) {failure({code});},
    }}), error => message.test(error.message) && /manually/.test(error.message));
  }
});

test('invalid browser coordinates never become a search center', async () => {
  for (const [latitude, longitude] of [[NaN, 0], [0, Infinity], [91, 0], [0, -181], ['40', 0]]) {
    await assert.rejects(currentLocation({secureContext: true, geolocation: {
      getCurrentPosition(success) {success({coords: {latitude, longitude}});},
    }}), /invalid location/);
  }
});

test('unanswered permission prompts finish and late fixes are ignored', async () => {
  let respond;
  const pending = currentLocation({secureContext: true, timeoutMs: 5, geolocation: {
    getCurrentPosition(success) {respond = success;},
  }});
  await assert.rejects(pending, /timed out/);
  respond({coords: {latitude: 40, longitude: -86, accuracy: 10}});
  await assert.rejects(pending, /timed out/);
});
