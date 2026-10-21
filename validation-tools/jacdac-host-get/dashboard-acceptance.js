void (async () => {
  const fileName = `197-dashboard-acceptance-${Date.now()}.json`;
  const download = value => {
    const text = JSON.stringify(value, null, 2);
    localStorage.setItem("jd197_acceptance", text);
    const link = document.createElement("a");
    link.href = URL.createObjectURL(new Blob([text], { type: "application/json" }));
    link.download = fileName;
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  };
  const hex = data => [...(data || [])]
    .map(byte => byte.toString(16).padStart(2, "0"))
    .join("");
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

  try {
    const bus = window.__jacdacBus;
    if (!bus) throw new Error("window.__jacdacBus is unavailable");
    const gyroscopeClass = 0x1e1b06f2;
    const device = bus.devices().find(candidate =>
      candidate.isPhysical && candidate.serviceClasses?.[1] === gyroscopeClass
    );
    if (!device) throw new Error("physical service-1 gyroscope device not found");
    const service = device.service(1);
    if (!service) throw new Error("service index 1 is unavailable");

    const startedAt = bus.timestamp;
    const reports = [];
    const requests = [];
    const reportHandler = packet => {
      if (
        packet.deviceIdentifier === device.deviceId &&
        packet.serviceIndex === 1 &&
        packet.isReport
      ) {
        reports.push({
          at: bus.timestamp,
          command: packet.serviceCommand,
          crc: packet.crc,
          crcOk: packet.crc !== 0,
          payloadHex: hex(packet.data),
          payloadLength: packet.data?.length || 0,
          rawHex: hex(packet.toBuffer()),
        });
      }
    };
    const requestHandler = packet => {
      requests.push({
        at: bus.timestamp,
        command: packet.serviceCommand,
        crc: packet.crc,
        rawHex: hex(packet.toBuffer()),
      });
    };
    bus.on("packetProcess", reportHandler);
    service.on("packetSend", requestHandler);

    await sleep(1100);
    const firstCommandAt = bus.timestamp;
    for (const command of [0x1004, 0x1102, 0x1101]) {
      await service.sendCmdAsync(command);
      await sleep(300);
    }
    const finalCommandAt = requests.findLast(request => request.command === 0x1101)?.at;
    await sleep(2300);

    bus.off("packetProcess", reportHandler);
    service.off("packetSend", requestHandler);

    const intervalResponse = reports.find(report =>
      report.at >= firstCommandAt &&
      report.command === 0x1004 &&
      report.payloadHex === "32000000"
    );
    const unsupportedRequest = requests.find(request => request.command === 0x1102);
    const unsupportedPayload = unsupportedRequest
      ? `0211${(unsupportedRequest.crc & 0xff).toString(16).padStart(2, "0")}${(unsupportedRequest.crc >>> 8).toString(16).padStart(2, "0")}`
      : undefined;
    const unsupportedResponse = reports.find(report =>
      report.at >= (unsupportedRequest?.at || firstCommandAt) &&
      report.command === 0x0003 &&
      report.payloadHex === unsupportedPayload
    );
    const readingRequest = requests.findLast(request => request.command === 0x1101);
    const readingResponse = reports.find(report =>
      report.at >= (readingRequest?.at || finalCommandAt) &&
      report.command === 0x1101 &&
      report.payloadLength === 12
    );
    const baselineStreaming = reports.filter(report =>
      report.at < firstCommandAt &&
      report.command === 0x1101 &&
      report.payloadLength === 12
    );
    const postStreaming = reports.filter(report =>
      readingResponse &&
      report.at > readingResponse.at &&
      report.at <= readingResponse.at + 2000 &&
      report.command === 0x1101 &&
      report.payloadLength === 12
    );
    const readingPayloads = reports
      .filter(report => report.command === 0x1101 && report.payloadLength === 12)
      .map(report => report.payloadHex);
    const result = {
      capturedAt: new Date().toISOString(),
      device: {
        deviceId: device.deviceId,
        firmwareVersion: device.firmwareVersion,
        friendlyName: device.friendlyName,
        productIdentifier: device.productIdentifier,
        serviceClasses: device.serviceClasses,
        service1Class: device.serviceClassAt(1),
        service1Name: service.name,
      },
      timing: {
        startedAt,
        firstCommandAt,
        finalCommandAt,
        commandSpacingMs: requests.slice(1).map((request, index) => request.at - requests[index].at),
      },
      checks: {
        announcementGyroscope: device.serviceClassAt(1) === gyroscopeClass,
        baselineStreamingCount1100ms: baselineStreaming.length,
        baselineStreamingRateHz: baselineStreaming.length / 1.1,
        intervalResponseOk: !!intervalResponse,
        unsupportedResponseOk: !!unsupportedResponse,
        readingResponseOk: !!readingResponse,
        postStreamingCount2000ms: postStreaming.length,
        postStreamingCountOk: postStreaming.length >= 36 && postStreaming.length <= 44,
        allReadingPayloads12B: readingPayloads.length > 0 && reports
          .filter(report => report.command === 0x1101)
          .every(report => report.payloadLength === 12),
        distinctReadingPayloads: new Set(readingPayloads).size,
      },
      expectedUnsupportedPayloadHex: unsupportedPayload,
      intervalResponse,
      unsupportedResponse,
      readingResponse,
      requests,
      reports,
    };
    download(result);
    const passed = result.checks.announcementGyroscope &&
      result.checks.intervalResponseOk &&
      result.checks.unsupportedResponseOk &&
      result.checks.readingResponseOk &&
      result.checks.postStreamingCountOk &&
      result.checks.allReadingPayloads12B;
    document.title = `JD197_${passed ? "PASS" : "FAIL"}_${fileName}`;
  } catch (error) {
    const result = {
      capturedAt: new Date().toISOString(),
      error: error?.stack || String(error),
    };
    download(result);
    document.title = `JD197_ERROR_${fileName}`;
  }
})();
