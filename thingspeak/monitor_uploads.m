% Monitor the weather channel and alert when uploads stop or recover.
% Copy this script into a ThingSpeak MATLAB Analysis app and replace the
% configuration values below. Trigger it from the inactivity and recovery
% React apps described in README.md.

sourceChannelID = 0;
sourceReadKey = '';

monitorChannelID = 0;
monitorReadKey = '';
monitorWriteKey = '';

alertApiKey = 'TAKXXXXXXXXXXXXXXXX';
staleAfterMinutes = 30;

if sourceChannelID <= 0 || monitorChannelID <= 0
    error('Set sourceChannelID and monitorChannelID before running.');
end
if isempty(monitorReadKey) || isempty(monitorWriteKey)
    error('Set the monitor channel Read and Write API keys before running.');
end
if isempty(alertApiKey) || startsWith(alertApiKey, 'TAKXXXX')
    error('Set the ThingSpeak Alerts API key before running.');
end

% Read the newest weather entry. A Read API key is optional for a public
% source channel and required for a private source channel.
if isempty(sourceReadKey)
    [~, sourceTimestamps] = thingSpeakRead(sourceChannelID, ...
        'Fields', 1, 'NumPoints', 1);
else
    [~, sourceTimestamps] = thingSpeakRead(sourceChannelID, ...
        'Fields', 1, 'NumPoints', 1, 'ReadKey', sourceReadKey);
end

nowUtc = datetime('now', 'TimeZone', 'UTC');
if isempty(sourceTimestamps)
    ageMinutes = -1;
    currentState = 1;
    latestReceivedText = 'No entries have been received.';
else
    latestReceived = sourceTimestamps(end);
    latestReceived.TimeZone = 'UTC';
    ageMinutes = max(0, minutes(nowUtc - latestReceived));
    currentState = double(ageMinutes >= staleAfterMinutes);
    latestReceived.Format = 'yyyy-MM-dd HH:mm:ss';
    latestReceivedText = sprintf('Latest entry: %s UTC (%.1f minutes ago).', ...
        char(latestReceived), ageMinutes);
end

% The monitor channel stores the current state on every analysis run:
%   field1: 0 = healthy, 1 = stopped
%   field2: minutes since the latest source entry (-1 = no source entries)
[previousValue, previousTimestamps] = thingSpeakRead(monitorChannelID, ...
    'Fields', 1, 'NumPoints', 1, 'ReadKey', monitorReadKey);

if isempty(previousTimestamps) || isempty(previousValue) || isnan(previousValue(end))
    previousState = -1;
else
    previousState = round(previousValue(end));
end

stateChanged = previousState ~= currentState;
if ~stateChanged
    thingSpeakWrite(monitorChannelID, ...
        'Fields', [1 2], ...
        'Values', [currentState ageMinutes], ...
        'WriteKey', monitorWriteKey);
    fprintf('No state change. State=%d. %s\n', currentState, latestReceivedText);
    return;
end

% Establish a healthy baseline without sending an initial recovery message.
sendAlert = ~(previousState == -1 && currentState == 0);
if currentState == 1
    alertSubject = 'M5Stack weather upload stopped';
    alertBody = sprintf(['No new ThingSpeak weather entry has arrived for ' ...
        'at least %d minutes. %s'], staleAfterMinutes, latestReceivedText);
else
    alertSubject = 'M5Stack weather upload recovered';
    alertBody = sprintf('ThingSpeak weather uploads have resumed. %s', ...
        latestReceivedText);
end

if sendAlert
    alertUrl = 'https://api.thingspeak.com/alerts/send';
    alertOptions = weboptions('HeaderFields', ...
        ["ThingSpeak-Alerts-API-Key", string(alertApiKey)]);

    % If alert delivery fails, do not persist the transition. The next
    % next React run can retry it instead of silently losing the alert.
    webwrite(alertUrl, 'body', alertBody, 'subject', alertSubject, ...
        alertOptions);
end

thingSpeakWrite(monitorChannelID, ...
    'Fields', [1 2], ...
    'Values', [currentState ageMinutes], ...
    'WriteKey', monitorWriteKey);

fprintf('State changed from %d to %d. Alert sent=%d. %s\n', ...
    previousState, currentState, sendAlert, latestReceivedText);
