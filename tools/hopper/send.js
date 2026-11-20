// send.js PID EXE SCRIPT [TIMEOUT_S]: Hopper's own "open executable" Apple event (Hopper.sdef HoppOpEx) sent to ONE Hopper process,
// with the PE loader, analysis on and "execute Python script" (EXPY). `open --args -Y` does not run the script, and the
// hopper launcher talks to whichever Hopper runs first (another session's too).
ObjC.import('Foundation');
function run(argv) {
  const cc = s => s.split('').reduce((a, c) => a * 256 + c.charCodeAt(0), 0);
  const t = $.NSAppleEventDescriptor.descriptorWithProcessIdentifier(parseInt(argv[0]));
  const ev = $.NSAppleEventDescriptor.appleEventWithEventClassEventIDTargetDescriptorReturnIDTransactionID(cc('Hopp'), cc('OpEx'), t, -1, 0);
  const file = p => $.NSAppleEventDescriptor.descriptorWithFileURL($.NSURL.fileURLWithPath(p));
  ev.setParamDescriptorForKeyword(file(argv[1]), cc('----'));
  ev.setParamDescriptorForKeyword(file(argv[2]), cc('EXPY'));
  ev.setParamDescriptorForKeyword($.NSAppleEventDescriptor.descriptorWithBoolean(true), cc('STAN'));
  const rec = $.NSAppleEventDescriptor.recordDescriptor;
  // Loader command identifier, as REA passes it (-l WinPE): "PE" or "Windows PE" matches none and the Loader dialog opens.
  rec.setDescriptorForKeyword($.NSAppleEventDescriptor.descriptorWithString('WinPE'), cc('LoLo'));
  const opts = $.NSAppleEventDescriptor.listDescriptor; opts.insertDescriptorAtIndex(rec, 1);
  ev.setParamDescriptorForKeyword(opts, cc('OPTS'));
  const err = Ref();
  const r = ev.sendEventWithOptionsTimeoutError($.NSAppleEventSendWaitForReply, parseInt(argv[3] || "30"), err);
  return r.isNil() ? 'error ' + (err[0] ? err[0].localizedDescription.js : '?') : 'sent ' + r.description.js.slice(0, 200);
}
