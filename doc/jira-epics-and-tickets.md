# Jira Epics and Tickets - GrowDirector v3

This document contains a comprehensive list of all epics and their associated tickets for the GrowDirector v3 project.

**Project:** [GDV1 - GrowDirector v1/v3](https://growdirector.atlassian.net/jira/software/projects/GDV1)  
**Creation Date:** January 31, 2026  
**Source:** Jira API (Direct from GDV1 project)  
**Total Epics:** 54

---

## 📋 Table of Contents

- [Epic Overview (All 54 Epics)](#epic-overview-all-54-epics)
- [Active & High Priority Epics](#active--high-priority-epics)
- [Completed Epics](#completed-epics)
- [Planned Epics](#planned-epics)
- [All Epics (Alphabetical)](#all-epics-alphabetical)
- [Quick Links](#quick-links)

---

## Epic Overview (All 54 Epics)

The GrowDirector v3 project has **54 epics** covering all aspects of development from initial features to current innovations:

### By Status:
- **To Do:** 17 epics
- **In Progress:** 8 epics  
- **Done:** 15 epics
- **Closed:** 13 epics
- **In Test:** 1 epic

### By Priority:
- **Highest:** 2 epics
- **High:** 1 epic
- **Medium:** 47 epics
- **Low:** 4 epics

---

## Active & High Priority Epics

These are the most important active epics:

| Epic Key | Name | Status | Priority | Created | Description |
|----------|------|--------|----------|---------|-------------|
| [**GDV1-2591**](https://growdirector.atlassian.net/browse/GDV1-2591) | **Workflow Automation & Sequencing** | In Progress | Highest | 2025-02-24 | Complex automation scenarios for greenhouse operations |
| [**GDV1-903**](https://growdirector.atlassian.net/browse/GDV1-903) | **Offline mode (modules & sensors data)** | In Progress | Highest | 2023-09-16 | Offline functionality for modules and sensors |
| [**GDV1-3133**](https://growdirector.atlassian.net/browse/GDV1-3133) | **SaaS** | In Progress | Medium | 2025-12-02 | SaaS subscriptions implementation |
| [**GDV1-2905**](https://growdirector.atlassian.net/browse/GDV1-2905) | **Summer/Winter Time [TimeZone]** | Closed | Medium | 2025-07-16 | Time zone and DST support |
| [**GDV1-3230**](https://growdirector.atlassian.net/browse/GDV1-3230) | **DLI** | To Do | Medium | 2026-01-29 | Daily Light Integral virtual sensor |
| [**GDV1-3229**](https://growdirector.atlassian.net/browse/GDV1-3229) | **Virtual Sensors** | To Do | Medium | 2026-01-29 | Generic virtual sensors framework |
| [**GDV1-2577**](https://growdirector.atlassian.net/browse/GDV1-2577) | **Trigger Delay** | Closed | High | 2025-02-20 | Delay before rule trigger activation |
| [**GDV1-294**](https://growdirector.atlassian.net/browse/GDV1-294) | **Agri Notes [GrowBook / GrowLog]** | In Progress | Medium | 2022-11-27 | Integrated journal and ERP for growers |

---

## 🔥 Detailed Epic Analysis

This section contains in-depth analysis of epics including descriptions, comments, and all related tickets.

---

### [GDV1-2591](https://growdirector.atlassian.net/browse/GDV1-2591) - Workflow Automation & Sequencing

**Status:** In Progress | **Priority:** Highest | **Created:** February 24, 2025

**Summary:**  
Competitive advantage feature for automated device action plans enabling sequential workflows for fans, heaters, pumps, and curtains. Desktop-only web interface for S3 boards due to mobile configuration complexity and S2 memory constraints. High priority for US distributor, with visual editor in active development using xyflow library. Estimated 80h frontend + 104h backend/firmware effort.

**Business Context:**  
- Cannot implement on mobile due to configuration complexity
- S2 boards have insufficient memory, requires S3
- Web interface planned for desktop workflow management  
- US distributor dependency driving timeline
- Targeted at bigger growers for competitive advantage
- Must prevent conflicts between workflows and existing rules

**Technical Details:**
- Visual workflow editor using xyflow library
- JSON-based workflow structure with node types: Trigger, Condition, Action, Delay, Loop
- Device restrictions and workflow validation required
- New Workflow resource separate from module-store
- API: isEnabled field for workflow activation, module workflowUid field
- Frontend: 80h estimate, Backend API: 24h, Firmware: 80h, Testing: 20h, Bugfixing: 20h

**Related Tickets (5):**

#### [GDV1-3231](https://growdirector.atlassian.net/browse/GDV1-3231) - API for Firmware and Backend Enhancements
**Status:** To Do | **Priority:** High | **Created:** January 29, 2026  
New ticket for firmware and backend API enhancements. No comments yet.

#### [GDV1-3227](https://growdirector.atlassian.net/browse/GDV1-3227) - API Improvements for Workflow
**Status:** Closed | **Priority:** High | **Created:** January 28, 2026  
**Description:** Added isEnabled boolean field for workflow activation, workflow processing logic in firmware, and module workflowUid field.  
**Comments:** Sergei added isEnabled checkbox to workflow edit form in admin panel.

#### [GDV1-3223](https://growdirector.atlassian.net/browse/GDV1-3223) - QA Test Cases for Workflow
**Status:** In Progress | **Priority:** Medium | **Created:** January 22, 2026  
**Description:** QA testing and bug reporting for workflow feature.  
**Key Issues Found (Marat, Jan 22):**
1. Cannot delete lines - need context menu on line click
2. Trigger edit problems with naming
3. When editing trigger with naming, can't complete - naming field affects editing process
4. Time trigger type unclear - show "Time" in Time Trigger block
5. Suggest line drawing button vs tiny connector points
6. Missing sensor type field in Trigger block when selecting specific sensors
7. New blocks stack on top invisibly - position calculation issue
8. Operation time and weekdays missing in sensor trigger configuration  
9. Logic Operation confusing for single condition
10. Condition block unclear - need indication that multiple sensors require Condition
11. "Trigger is required" tooltip needed

**Resolution Status:** Alexander marked several issues as DONE, others in progress.

#### [GDV1-3174](https://growdirector.atlassian.net/browse/GDV1-3174) - Workflow Data Format Specification
**Status:** Done | **Priority:** High | **Created:** December 16, 2025  
**Description:** Complete JSON schema for workflow data structure including:
- **Node Types:** Trigger (Sensor/Device/Time/DateRange), Condition (And/Or logic), Action (device control), Delay (time-based), Loop (iterations)
- **Connection Types:** next, successNext, failNext
- **Configuration Parameters:** All node type-specific config fields
- **Workflow Structure:** nodes array, connections array, position coordinates

#### [GDV1-2615](https://growdirector.atlassian.net/browse/GDV1-2615) - Workflow Sequences (Main Implementation)
**Status:** In Progress | **Priority:** High | **Created:** March 3, 2025  
**Description:** Automated action plans for devices - main implementation ticket.

**Development Timeline (9 comments):**
1. **Initial Plan (Alexander, Mar 18):** Rough estimate 80h frontend, unknown API/firmware
2. **Detailed Estimates (Arsen, Mar 20):** Backend API 24h, Firmware 80h, Testing 20h, Bugfixing 20h  
3. **Resource Creation (Arsen, Mar 21):** New Workflow resource instead of module-store modifications
4. **Data Format Evolution:** JSON schema with nodes, connections, positions
5. **Field Additions:** Position field for node placement, notBetween operator support
6. **Validation Requirements:**
   - Cannot activate workflow if rules exist for same devices
   - Device restrictions per module capabilities
   - Rules stop processing when workflow active
7. **UI Improvements:**
   - Remove manual device control when workflow active
   - Verify device availability before activation
   - OR logic for multiple triggers
   - Hide endTime for workflows
   - Weekdays selection in trigger blocks
   - Device badges showing workflow association
   - Workflow badges on devices

**Current Status:** Active development, visual editor implementation in progress, QA feedback being addressed.

---

### [GDV1-903](https://growdirector.atlassian.net/browse/GDV1-903) - Offline Mode (Modules & Sensors Data)

**Status:** In Progress | **Priority:** Highest | **Created:** September 16, 2023

**Summary:**  
Comprehensive offline functionality enabling modules to operate, store sensor data, and maintain logs without internet connectivity. Long-running initiative (since Sept 2023) implementing sparse algorithm for efficient sensor data storage (5min→30min→1hr as memory fills), multiple log types for diagnostics, MQTT state tracking improvements, and offline data transmission when connectivity restored.

**Technical Details:**
- **File System Dependency:** Requires proper file system initialization on modules
- **Sparse Algorithm:** Progressive data reduction (5min → 30min → 1hr intervals) as memory fills, ~500 values capacity
- **Log Types:**
  - FILE_LOG: Always present operational logs
  - RESTART_FILE_LOG: Created on crashes/unexpected restarts
  - STARTUP_FILE_LOG: Boot logs with initialization data
  - DIAGNOSTIC_FILE_LOG: Network problem diagnostics
- **MQTT State Tracking:** Diagnostic packets for better online/offline detection
- **History Data:** Proper transmission format with accountUid, no consecutive duplicates
- **Elasticsearch Verification:** Testing requires checking history topic for offline period data

**Implementation Versions:**
- Sensor data saving: Firmware 1.6.1+
- History data fixes: Firmware 1.7.11
- MQTT improvements: Ongoing

**Related Tickets (7):**

#### [GDV1-1847](https://growdirector.atlassian.net/browse/GDV1-1847) - Improve Module State Tracking
**Status:** Closed | **Priority:** Medium | **Created:** July 17, 2024  
**Description:** Improve module state tracking via MQTT diagnostic packets.  
**Testing Issues (Marat, Jul 17):** Module stayed online during MQTT requests but offline notification triggered, sensor showed offline in app. Unclear behavior during testing.  
**Resolution (Arsen):** Change timestamp in MQTT test requests for proper state detection.

#### [GDV1-1837](https://growdirector.atlassian.net/browse/GDV1-1837) - Module Sends Incorrect History Data
**Status:** Closed | **Priority:** High | **Created:** July 7, 2024  
**Problem:** Module sending non-transition data to history topic, missing accountUid field.  
**Testing Requirements (Arsen, 4 comments):**
1. Test manual device control + rules in both online and offline scenarios
2. Verify on-off alternation in Elasticsearch (no consecutive duplicates allowed)
3. Check accountUid presence in all history records
4. Create regression test for this scenario  
**Resolution:** Fixed in firmware 1.7.11

#### [GDV1-1805](https://growdirector.atlassian.net/browse/GDV1-1805) - Enhance Log Files
**Status:** Closed | **Priority:** Medium | **Created:** June 18, 2024  
**Description:** Enhance STARTUP_FILE_LOG, RESTART_FILE_LOG, FILE_LOG:  
- Add timestamps to all log entries
- Remove unused information
- Optimize storage usage
- Increase log file size capacity

#### [GDV1-1304](https://growdirector.atlassian.net/browse/GDV1-1304) - Process Offline Sensor Data
**Status:** In Progress | **Priority:** Highest | **Created:** February 6, 2024  
**Description:** Core functionality for processing sensor data collected during offline periods. Sparse algorithm implementation and regression testing.  
**Status:** Critical ongoing work for offline mode completion.

#### [GDV1-881](https://growdirector.atlassian.net/browse/GDV1-881) - Send Offline Logs by Module
**Status:** Closed | **Priority:** High | **Created:** December 20, 2023  
**Description:** Module sends logs collected during offline period when connectivity restored.  
**Testing Plan (Arsen, Dec 21, 2023):**
- Test on firmware v0.70.0
- Requires file system initialization
- Verify 4 log type scenarios:
  1. **FILE_LOG:** Always present
  2. **RESTART_FILE_LOG:** Created on crashes/unexpected restarts
  3. **STARTUP_FILE_LOG:** Created on boot with initialization data
  4. **DIAGNOSTIC_FILE_LOG:** Created on network problems

#### [GDV1-836](https://growdirector.atlassian.net/browse/GDV1-836) - Save Sensor Data When Offline
**Status:** Closed | **Priority:** Medium | **Created:** December 25, 2023  
**Description:** Add saving sensor data when offline with sparse algorithm:
- Initial frequency: every 5 minutes
- Progressive reduction: every 30 minutes when 50% full
- Final reduction: every 1 hour when 75% full
- Capacity limit: ~500 values

**Testing Plan (Arsen, Dec 25, 2023):**
1. Disconnect internet for 1 hour
2. Reconnect internet
3. Verify LED behavior (blue LED sequence)
4. Check app shows module online
5. Verify history topic in Elasticsearch has ~12 records (1 per 5 min)

**Testing Results (Marat, Jan 16, 2024):**
- Module went offline and online correctly
- History topic missing in Elasticsearch - issue found

**Resolution:** Fixed in firmware 1.6.1+

#### [GDV1-774](https://growdirector.atlassian.net/browse/GDV1-774) - Chart Display for Short Offline Periods
**Status:** To Do | **Priority:** Medium | **Created:** October 24, 2023  
**Description:** If module offline < 10-15 minutes, don't show offline dot on chart. Make this time setting configurable with min/max display thresholds to discuss.

**Current Status:** Long-running epic with most core functionality delivered, GDV1-1304 (offline sensor data processing) remains critical in-progress work.

---

### [GDV1-2577](https://growdirector.atlassian.net/browse/GDV1-2577) - Trigger Delay

**Status:** Closed | **Priority:** High | **Created:** February 20, 2025 | **Closed:** May 14, 2025

**Summary:**  
Successfully delivered feature enabling configurable time delays (up to 60 minutes) before rule trigger activation. Prevents rapid device cycling caused by sensor value fluctuations by verifying conditions remain valid during delay period. Implemented with new device states (delayOn/delayOff), comprehensive state machine logic, and extensive regression testing. Delivered in firmware 1.12.10+ and App 1.12.1-1.12.2.

**Business Problem:**  
Rapid sensor fluctuations around trigger thresholds caused frequent device on/off cycling, reducing equipment lifespan and creating operational issues. Solution provides configurable delay to verify condition persistence before activation.

**Technical Implementation:**
- **New Device States:** delayOn (waiting to turn ON), delayOff (waiting to turn OFF)
- **API Fields:** delayOn and delayOff parameters in sensor rules and step rules
- **Validation:** Maximum 3600 seconds (60 minutes), disabled if 0 or null
- **State Machine:** 6 state transitions with timer reset on condition change
- **Scope:** Sensor rules only (not for manual control)

**State Transitions:**
1. **OFF → DelayOn:** Sensor crosses ON threshold, start delayOn timer
2. **DelayOn → OFF:** Sensor returns below threshold during delay, cancel timer
3. **DelayOn → ON:** DelayOn timer expires, activate device
4. **ON → DelayOff:** Sensor crosses OFF threshold, start delayOff timer
5. **DelayOff → ON:** Sensor returns above threshold during delay, cancel timer
6. **DelayOff → OFF:** DelayOff timer expires, deactivate device

**UI/UX:**
- Delay status indication added in App 1.12.1
- Countdown indicator NOT implemented (parameters not passed from module, time may reset)
- Status display priority: Trigger delay shown as main status vs Device idle

**Related Tickets (6):**

#### [GDV1-2916](https://growdirector.atlassian.net/browse/GDV1-2916) - No Countdown Indicator During Trigger Delay
**Status:** Closed | **Priority:** Medium | **Created:** July 30, 2025  
**Problem:** No visual countdown during delay wait period.  
**Decision (Alexander, Jul 30):** Cannot display countdown because:
- Delay parameters not passed from module/API to UI
- No exact time available
- Timer may reset when sensor crosses threshold boundary
- Won't implement unless user specifically requests

#### [GDV1-2729](https://growdirector.atlassian.net/browse/GDV1-2729) - Add Regression Test Cases
**Status:** Closed | **Priority:** Medium | **Created:** April 28, 2025  
**Description:** Comprehensive manual test case documentation covering all scenarios.

**Test Coverage (Marat, Apr 28):**
1. **HydroDirector:**
   - Basic pump activation with delay
   - Delay interruption scenarios
   - cyclesToSetPoint parameter integration
   - Multiple pumps with different delays

2. **DryContactDirector:**
   - Heater/fan activation with delays
   - Delay interruption with rapid sensor changes
   - Multiple devices (heaters + fans)
   - Schedule integration with delays

3. **SocketDirector:**
   - Same test scenarios as DryContactDirector

4. **Step Rules:**
   - Step activation with delays
   - Delay interruption during step execution
   - Mixed sensor types in step rules
   - Schedule interaction with step delays

5. **Edge Cases:**
   - Zero delay (0 seconds) = immediate activation
   - Maximum delay (3600 seconds) = 1 hour
   - Delay behavior during offline period
   - Manual override during delay wait
   - Operation time expiration during delay
   - Multiple rules with different delays for same device

**Approval:** Alexander approved for regression tests (May 1, 2025)

#### [GDV1-2700](https://growdirector.atlassian.net/browse/GDV1-2700) - No Indication of Trigger Delay Operation
**Status:** Closed | **Priority:** Medium | **Created:** April 10, 2025  
**Problem:** No visual indication in app when trigger delay function is active for step rules.  
**Resolution (Alexander):** Added indication in App 1.12.1

#### [GDV1-2650](https://growdirector.atlassian.net/browse/GDV1-2650) - Add Rule Triggering Delay to API
**Status:** Closed | **Priority:** Medium | **Created:** March 20, 2025  
**Description:** API implementation for trigger delay feature:  
- New fields: delayOn and delayOff for sensor rules and step rules
- Validation: Maximum 3600 seconds
- New device statuses: delayOn and delayOff
- Backend logic for delay state management

#### [GDV1-2582](https://growdirector.atlassian.net/browse/GDV1-2582) - Delayed Activation for Device Operations
**Status:** Closed | **Priority:** Medium | **Created:** February 20, 2025  
**Problem Statement:** Rapid sensor value fluctuations near trigger thresholds cause devices to turn on/off frequently, leading to:
- Reduced equipment lifespan
- Unnecessary power consumption
- Operational instability
- User frustration

**Solution:** Configurable delay parameter (Y minutes) to verify sensor condition remains valid before device activation. If sensor value returns to normal range during delay period, activation is cancelled.

#### [GDV1-2578](https://growdirector.atlassian.net/browse/GDV1-2578) - Trigger Delay Solution (Main Ticket)
**Status:** Closed | **Priority:** High | **Created:** February 20, 2025  
**Description:** Main solution ticket with complete implementation details.

**Implementation Details (10 comments, Feb-Apr 2025):**

**Requirements (Alexander, Feb 20):**
- New device statuses: waiting for ON / waiting for OFF
- 2 parameters: delayOn and delayOff

**Technical Specifications (Arsen, Feb 21):**
- Parameter unit: seconds (not minutes)
- Maximum: 3600 seconds (60 minutes)
- Disabled: if 0, null, or not set
- Scope: Sensor rules only

**State Machine (Alexander, Feb 24):**
| Current State | Sensor Condition | New State | Description |
|--------------|------------------|-----------|-------------|
| OFF | Crosses ON threshold | DelayOn | Start delayOn timer, show "waiting for ON" |
| DelayOn | Returns below ON threshold | OFF | Cancel timer, return to OFF |
| DelayOn | Timer expires | ON | Activate device |
| ON | Crosses OFF threshold | DelayOff | Start delayOff timer, show "waiting for OFF" |
| DelayOff | Returns above OFF threshold | ON | Cancel timer, stay ON |
| DelayOff | Timer expires | OFF | Deactivate device |

**Additional Logic:**
- Timer resets on each state change
- Operation time expiration: If operating time ends during delay, device turns OFF immediately
- DelayOff setting removed from UI (use only delayOn)
- Status display: Trigger delay shown as main status instead of "Device idle"

**Delivery Timeline:**
- App 1.12.0: Initial implementation
- App 1.12.1: UI indication added, testing ongoing
- App 1.12.2: Final version
- Firmware: 1.12.8, 1.12.9, 1.12.10 tested

**Testing Confirmation (Yuri, Apr 2025):**
- Tested on firmware 1.12.8, 1.12.9, 1.12.10
- Tested Dry Contact and Socket modules
- Tested on both Android and iOS apps
- All scenarios working correctly

**Final Status:** Successfully delivered and closed May 14, 2025. Feature working in production with firmware 1.12.10+ and App 1.12.1-1.12.2.

---

### [GDV1-827](https://growdirector.atlassian.net/browse/GDV1-827) - Admin Panel - Statistics, Analytics & Management

**Status:** In Progress | **Priority:** Medium | **Created:** August 22, 2023

**Summary:**  
Production-ready admin portal for system management, analytics, and operations deployed at panel.growdirector.com. Provides comprehensive statistics tracking (rules per device, user/module activity), firmware management with error handling, AI Reports configuration with localization support, and custom channel management. Auto-login for dev/stage environments with server switching capability. Two design-focused tickets remain for enhanced analytics portal with map visualization.

**Business Context:**  
- Full-featured admin panel in production (panel.growdirector.com)
- Critical for system operations, monitoring, and support
- Statistics enable data-driven product decisions
- AI Reports require configuration management with multi-region support
- Test account exclusion ensures accurate metrics

**Technical Features:**
- **URL:** panel.growdirector.com  
- **Authentication:** Auto-login to dev/stage, manual for production, badge click for server switching
- **Statistics API:** /Rules, /Rules/detailed, /Rules/timeline with byCount parameter (rules per device)
- **User/Module Tracking:** New users/modules, activity timelines, offline periods, firmware versions
- **AI Reports Config:** Timezone selector, unit measurement (metric/imperial), language selector, [wiki guide](https://growdirector.atlassian.net/wiki/...)
- **Firmware Management:** Upload, download, delete with error handling for validation failures
- **Custom Channels:** CRUD operations for custom device channels
- **Test Account Filter:** 11 specified accounts excluded from statistics

**Related Tickets (15):**

#### [GDV1-3179](https://growdirector.atlassian.net/browse/GDV1-3179) - Handle Error on Firmware Update Command
**Status:** Closed | **Priority:** Low | **Created:** January 5, 2026  
**Problem:** Backend errors not handled properly in admin panel during firmware update. Validation errors occurred silently.  
**Links:** Screenshot and monitoring logs provided for debugging.

#### [GDV1-2958](https://growdirector.atlassian.net/browse/GDV1-2958) - Add 'byCount' to Admin Statistics Query
**Status:** Closed | **Priority:** High | **Created:** August 20, 2025  
**Description:** Add statistics showing number of rules per device to three endpoints:  
- /api/v1/Statistics/Rules  
- /api/v1/Statistics/Rules/detailed  
- /api/v1/Statistics/Rules/timeline

**Comment (Alexander, Aug 20):** Request for third graph displaying byCount metric alongside existing charts.

#### [GDV1-2799](https://growdirector.atlassian.net/browse/GDV1-2799) - Add AI-Reports to Admin Panel
**Status:** Closed | **Priority:** Medium | **Created:** June 11, 2025  
**Description:** Create management section for AI report configurations including create, edit, filter, and delete operations.

**Comments:**
1. **Alexander (Jun 11):** Add three critical selectors:
   - Timezone selector for proper report timing
   - Unit measurement selector (metric vs imperial)
   - Language selector for localized reports

2. **Alexander (Jun 30):** Provided link to [AI Reports Configuration Guide](wiki page) documenting the feature.

#### [GDV1-2704](https://growdirector.atlassian.net/browse/GDV1-2704) - Exclude TEST Accounts from Statistics
**Status:** Closed | **Priority:** Medium | **Created:** April 7, 2025  
**Description:** Filter out 11 test accounts from all statistics calculations to ensure production metrics accuracy. List of specific test account IDs provided in ticket.

#### [GDV1-2613](https://growdirector.atlassian.net/browse/GDV1-2613) - Add Statistics Section to Admin Panel Using API
**Status:** Closed | **Priority:** High | **Created:** March 3, 2025  
**Description:** Initial implementation of statistics section consuming backend statistics API.

**Comment (Alexander, Apr 23):** "Need to test & fix bugs in API" - identified issues during initial rollout requiring backend fixes.

#### [GDV1-2600](https://growdirector.atlassian.net/browse/GDV1-2600) - Statistics by Accounts, Modules Activities
**Status:** Closed | **Priority:** Medium | **Created:** March 1, 2025  
**Description:** Track and visualize user account and module activity metrics including:
- New user registrations over time
- New module connections
- Module inactivity detection (offline periods)
- Active modules visualization timeline

**Comment (Sergei, Mar 6):** Extensive implementation notes covering:

**User Activity Events:**
- Registration timestamps
- First activity detection
- Last activity tracking
- Module connection events (first/subsequent)

**Module Activity Events:**
- First connection timestamp
- Last connection detection
- Offline period tracking
- Firmware version transitions
- Module lifecycle states

**Visualization Requirements:**
- Timeline charts for activity trends
- Heatmaps for peak usage periods
- Module distribution by firmware version
- Geographic distribution (if location data available)
- Retention cohort analysis

#### [GDV1-1563](https://growdirector.atlassian.net/browse/GDV1-1563) - Add Logo with Server Selection Functionality
**Status:** Closed | **Priority:** Medium | **Created:** April 15, 2024  
**Description:** Add GrowDirector logo to admin panel with server environment switching capability.

**Comment (Maxim, Apr 1):** Implementation details:
- Login page: Always connects to production
- Admin panel: Auto-logs to dev/stage environments
- Server switching: Click badge to toggle between environments
- Visual indicator: Badge shows current environment (DEV/STAGE/PROD)

#### [GDV1-1527](https://growdirector.atlassian.net/browse/GDV1-1527) - Error Downloading Firmware from Admin Panel
**Status:** Closed | **Priority:** Highest | **Created:** April 1, 2024  
**Problem:** Critical bug preventing firmware binary downloads from admin panel. Users unable to retrieve firmware files for distribution or archival.  
**Impact:** Blocked firmware management workflow.  
**Resolution:** Fixed download endpoint and file serving logic.

#### [GDV1-1436](https://growdirector.atlassian.net/browse/GDV1-1436) - Deploy Admin Panel to Production
**Status:** Closed | **Priority:** Medium | **Created:** February 21, 2024  
**Description:** Production deployment of admin panel to panel.growdirector.com with proper DNS, SSL certificates, and production database connections.  
**Outcome:** Successfully deployed and operational.

#### [GDV1-1362](https://growdirector.atlassian.net/browse/GDV1-1362) - Research Ability to Get Data for Admin Panel
**Status:** Closed | **Priority:** Low | **Created:** January 31, 2024  
**Description:** Initial research phase for admin panel data requirements covering:
- **Users:** Account management, activity tracking, authentication logs
- **Devices:** All device types, states, operation history
- **Modules:** Hardware details, firmware versions, connectivity status
- **Sensors:** Sensor types, readings history, calibration data

**Outcome:** Documented API requirements and data models for admin panel backend.

#### [GDV1-1325](https://growdirector.atlassian.net/browse/GDV1-1325) - Add Firmware Management
**Status:** Closed | **Priority:** Medium | **Created:** January 19, 2024  
**Description:** Complete CRUD operations for firmware binaries:
- **Create:** Upload new firmware versions with metadata (version number, release notes, target hardware)
- **Read:** List all firmware versions with filtering/sorting
- **Update:** Modify firmware metadata (cannot update binary after upload for integrity)
- **Delete:** Remove firmware versions (with validation preventing deletion of in-use versions)

#### [GDV1-1305](https://growdirector.atlassian.net/browse/GDV1-1305) - Deploy Adminka
**Status:** Closed | **Priority:** Medium | **Created:** January 16, 2024  
**Description:** Initial deployment of admin panel ("Adminka" - Russian diminutive for admin) to development environment for testing and iteration before production rollout.

#### [GDV1-1252](https://growdirector.atlassian.net/browse/GDV1-1252) - Add Custom Channels to Admin Panel
**Status:** Closed | **Priority:** Medium | **Created:** December 30, 2023  
**Description:** Custom device channel management functionality:
- **List View:** Display all custom channels with filtering
- **Add:** Create new custom channels with configuration
- **Update:** Modify existing channel properties
- **Delete:** TODO referenced in comments (deletion functionality planned separately)

#### [GDV1-826](https://growdirector.atlassian.net/browse/GDV1-826) - Design & Develop Admin Portal
**Status:** To Do | **Priority:** Medium | **Created:** August 25, 2023  
**Description:** Original foundational ticket for admin portal design and architecture. Despite functional portal in production, this ticket remains To Do indicating ongoing design improvements and architectural enhancements planned for future iterations.

#### [GDV1-740](https://growdirector.atlassian.net/browse/GDV1-740) - Portal with Analytics
**Status:** To Do | **Priority:** Medium | **Created:** July 13, 2023  
**Description:** Advanced analytics portal with map-based visualization showing:
- Module geographic distribution
- Regional activity heatmaps  
- Device density by location
- User demographics overlay
- Real-time system health dashboard
- Performance metrics by region

**Status:** Remains To Do as ambitious expansion of current admin capabilities.

---

### [GDV1-948](https://growdirector.atlassian.net/browse/GDV1-948) - Add Sensors Soil / LUX / PAR

**Status:** In Progress | **Priority:** Medium | **Created:** December 2, 2023

**Summary:**  
Sensor expansion initiative adding support for PAR (Apogee SQ-512-SS), LUX (RIKA RK-210-01), and Soil moisture (RIKA RK510-01) sensors. Implementation complete with API endpoint `/api/v1/Sensor/{uid}/set_type` and MQTT `SensorTypeCommandNotification` for firmware synchronization. Calibration duration set to 120 seconds. Critical issue identified: soil moisture instability (79%-96% fluctuation) requiring 5-minute threshold delay before irrigation activation to prevent false triggers.

**Business Context:**  
- Expanding sensor portfolio for comprehensive greenhouse monitoring
- PAR/LUX sensors enable precise light management for DimmerDirector
- Soil moisture critical for automated irrigation
- Data stability improvements needed to prevent false irrigation events
- Multiple firmware iterations (1.6.16, 1.6.31, 1.6.32) addressing sensor reliability

**Technical Details:**
- **API:** `POST /api/v1/Sensor/{uid}/set_type` with sensor type name
- **MQTT:** `SensorTypeCommandNotification` for firmware updates
- **Sensor Types:** 
  - PAR: Apogee SQ-512-SS (photosynthetically active radiation)
  - LUX: RIKA RK-210-01 (illuminance measurement)
  - Soil Moisture: RIKA RK510-01 (volumetric water content)
- **Calibration:** 120-second duration for PAR/LUX sensors
- **Firmware Versions:** 1.6.16 (initial support), 1.6.31, 1.6.32 (stability fixes)
- **DB Migration:** Added calibration time field (0 → 120s) for sensor types
- **Known Issue:** Soil moisture fluctuates 79%-96%, needs backend delay logic

**Related Tickets (11):**

#### [GDV1-2448](https://growdirector.atlassian.net/browse/GDV1-2448) - Add support for LUX sensor CC-A03
**Status:** To Do | **Priority:** Medium | **Created:** December 5, 2024  
**Description:** Additional LUX sensor model support request. Implementation pending.

#### [GDV1-2423](https://growdirector.atlassian.net/browse/GDV1-2423) - Soil moisture sensor is unstable
**Status:** To Do | **Priority:** Medium | **Created:** November 8, 2024  
**Problem:** Soil moisture readings fluctuate wildly between 79% and 96% even after calibration, causing unreliable irrigation triggers.

**Comments (9 total):**
1. **Alexander (Nov 8):** "Please look in logs and add values before/after calibration"
2. **Noam (Nov 9):** "The data from the sensor isn't steady enough to trigger an irrigation pump reliably. The pump should only turn on if soil moisture stays consistently below the threshold (e.g., 75%) for at least 5 minutes, rather than reacting instantly to brief dips. Also, add visible indicators (like a countdown or 'Waiting to activate' message) so users know the system is monitoring but waiting for a stable reading before acting."
3. **Dmytro (Nov 10):** "The calibration was done right. Will create a separate task for improving sensor data transmission timing."

**Status:** To Do, awaiting backend implementation of 5-minute threshold logic with visual feedback.

#### [GDV1-1721](https://growdirector.atlassian.net/browse/GDV1-1721) - Calibration on LUX stopped working
**Status:** Closed | **Priority:** Medium | **Created:** March 18, 2024  
**Problem:** LUX sensor calibration screen became unresponsive.  
**Solution (Comment):** "Перезагрузил модуль и выгрузил приложение" (Reloaded module and restarted app) - resolved after module reboot.

#### [GDV1-1720](https://growdirector.atlassian.net/browse/GDV1-1720) - After removing module through mobile app, the sensor assigned to it continues to remain
**Status:** Closed | **Priority:** Medium | **Created:** March 18, 2024  
**Problem:** Orphaned sensors remain after module deletion.  
**Solution (Comment):** "fixed in 1.6.32" - module deletion now properly cascades to sensors.

#### [GDV1-1709](https://growdirector.atlassian.net/browse/GDV1-1709) - The sensors Par and LUX are not working
**Status:** Closed | **Priority:** High | **Created:** March 13, 2024  
**Problem:** Both PAR and LUX sensors non-functional after installation.  
**Solution (Comment):** "fixed in ver 1.6.31" - firmware bug fixed.

#### [GDV1-1708](https://growdirector.atlassian.net/browse/GDV1-1708) - When switching to calibration on a temperature sensor, I was taken to a LUX sensor
**Status:** Closed | **Priority:** High | **Created:** March 13, 2024  
**Problem:** Navigation bug causing sensor type confusion during calibration flow.  
**Description:** Extensive log analysis provided showing incorrect sensor UID routing. Fixed in subsequent release.

#### [GDV1-1654](https://growdirector.atlassian.net/browse/GDV1-1654) - White screen when entering LUX Sensor calibration
**Status:** Closed | **Priority:** Medium | **Created:** February 29, 2024  
**Problem:** App crashes when entering LUX calibration screen.  
**Solution (2 comments):** Database migration required to add calibration time field. Updated sensor type record from 0 to 120 seconds, resolving the crash.

#### [GDV1-1643](https://growdirector.atlassian.net/browse/GDV1-1643) - Set sensor type for analog sensors
**Status:** Closed | **Priority:** Highest | **Created:** February 22, 2024  
**Description:** Enable sensor type selection for analog sensors excluding DO/ORP/pH/EC (which require specific hardware). Implementation complete with available sensor types list.

#### [GDV1-1615](https://growdirector.atlassian.net/browse/GDV1-1615) - Backend support for sensors
**Status:** Closed | **Priority:** Medium | **Created:** February 8, 2024  
**Description:** Backend API implementation for sensor type management. Covers points 1 & 2 from GDV1-1474 specification.

#### [GDV1-1614](https://growdirector.atlassian.net/browse/GDV1-1614) - Module support for sensors
**Status:** Closed | **Priority:** Medium | **Created:** February 8, 2024  
**Description:** Firmware implementation for sensor type support.  
**Solution (Comment):** "done in version 1.6.16" - initial firmware release with sensor type handling.

#### [GDV1-1474](https://growdirector.atlassian.net/browse/GDV1-1474) - Describe sensor type set process
**Status:** Closed | **Priority:** Medium | **Created:** December 28, 2023  
**Description:** Complete specification for sensor type assignment flow:

**API Command:**
```http
POST /api/v1/Sensor/{uid}/set_type
Body: { "sensorTypeName": "PAR" }
```

**MQTT Notification to Firmware:**
```json
{
  "notification": "SensorTypeCommandNotification",
  "data": {
    "sensorUid": "...",
    "sensorTypeName": "PAR"
  }
}
```

**Process Flow:**
1. Admin API receives sensor type assignment
2. Backend validates sensor type exists
3. MQTT command sent to module firmware
4. Firmware updates sensor configuration
5. Sensor starts reporting with new type metadata

---

### [GDV1-49](https://growdirector.atlassian.net/browse/GDV1-49) - Sensor calibration

**Status:** In Progress | **Priority:** Medium | **Created:** October 28, 2022

**Summary:**  
Comprehensive calibration system for pH, EC, DO, ORP, and Soil moisture sensors. All sensor types now have complete two-point calibration workflows with validation ranges, out-of-range notification system, and calibration history tracking. Validation ranges refined through multiple iterations (pH: 1500-2600/2600-3700, EC: 180-750/1100-3400) based on field testing. Admin API includes `FirstRawValueIsOutOfRange` and `SecondRawValueIsOutOfRange` flags for calibration error handling.

**Business Context:**  
- Critical for sensor accuracy and reliability in production environments
- Two-point calibration ensures linear sensor response across operating range
- Validation ranges prevent incorrect calibrations that could damage crops
- Notification system alerts users to calibration issues immediately
- Calibration history enables troubleshooting and sensor drift analysis
- All 5 sensor types (pH, EC, DO, ORP, Soil) now have full calibration support

**Technical Details:**
- **Calibration Type:** Two-point linear calibration for all sensors
- **Duration:** 120 seconds per calibration point
- **Validation Ranges:**
  - **pH First:** 1500-2600 (pH 4.00 buffer)
  - **pH Second:** 2600-3700 (pH 7.00 buffer)
  - **EC First:** 180-750 (1413 μS/cm buffer)
  - **EC Second:** 1100-3400 (12880 μS/cm buffer)
  - **DO/ORP:** Similar validation implemented
  - **Soil Moisture:** Min/max calibration (dry/saturated conditions)
- **Admin API Flags:**
  - `FirstRawValueIsOutOfRange`: boolean
  - `SecondRawValueIsOutOfRange`: boolean
- **Features:**
  - Pop-up notifications for out-of-range calibrations
  - Calibration status indicators in UI
  - Historical calibration data storage
  - Test account filtering for accurate metrics

**Related Tickets (9):**

#### [GDV1-2003](https://growdirector.atlassian.net/browse/GDV1-2003) - Pop up notification when calibrating pH
**Status:** Closed | **Priority:** Medium | **Created:** October 9, 2024  
**Description:** Add notification when pH calibration values fall outside valid range to prevent incorrect calibrations.

**Comments (5 total):**
1. **Sergei (Oct 10):** "Updated pH validation ranges based on testing:
   - First value: 1500-2600 (was 1700-2400)
   - Second value: 2600-3700 (was 2700-3400)
   These ranges better accommodate sensor variations while preventing bad calibrations."
2. **Alexander (Oct 11):** Confirmed implementation in app with clear error messaging.

#### [GDV1-1531](https://growdirector.atlassian.net/browse/GDV1-1531) - Send a notification to the user if the calibration value is in an error state
**Status:** Closed | **Priority:** Medium | **Created:** January 15, 2024  
**Description:** Comprehensive notification system for all calibration errors across all sensor types.

**Comments (18 total):**
1. **Noam (Jan 15):** "From Noam: After getting the same answer for some clients I assume we need this notification. When the value is out of range during calibration, the user needs to know about it."

2. **Alexander (Jan 16):** "Should be discussed with the team. Need to define exact thresholds for each sensor type and error message templates."

3. **Sergei (Jan 20):** Proposed initial validation ranges:
   ```
   pH First: 1700-2400
   pH Second: 2700-3400
   EC First: 200-700
   EC Second: 1200-3200
   ```

4. **Yuri (Jan 25-30):** Multiple test reports with screenshots showing notification behavior on dev environment.

5. **Sergei (Feb 5):** "Final validation ranges updated after field testing. Admin API now includes FirstRawValueIsOutOfRange and SecondRawValueIsOutOfRange boolean flags."

6. **Alexander (Feb 10):** "Need to exclude test accounts from calibration statistics. User IDs provided separately."

**Final Status:** Complete notification system with refined validation ranges, admin API flags, and test account filtering. Closed February 15, 2024.

#### [GDV1-867](https://growdirector.atlassian.net/browse/GDV1-867) - Add Soil moisture Calibration screen to Mobile App
**Status:** Closed | **Priority:** Medium | **Created:** September 4, 2023  
**Description:** Two-point calibration UI for soil moisture sensors (dry/saturated).  
**Solution (Comment):** "Done in 1.2.6" - calibration screen added to mobile app.

#### [GDV1-96](https://growdirector.atlassian.net/browse/GDV1-96) - Add sensor data endpoint
**Status:** Closed | **Priority:** Medium | **Created:** December 7, 2022  
**Description:** API endpoint for retrieving sensor calibration data and history. Implementation complete.

#### [GDV1-84](https://growdirector.atlassian.net/browse/GDV1-84) - Create the design of the sensor calibration process for ORP
**Status:** Closed | **Priority:** Medium | **Created:** November 15, 2022  
**Description:** UI/UX design for ORP sensor calibration flow. Design approved and implemented.

#### [GDV1-83](https://growdirector.atlassian.net/browse/GDV1-83) - Create the design of the sensor calibration process for DO
**Status:** Closed | **Priority:** Medium | **Created:** November 15, 2022  
**Description:** UI/UX design for dissolved oxygen sensor calibration flow. Design approved and implemented.

#### [GDV1-58](https://growdirector.atlassian.net/browse/GDV1-58) - Add ORP calibration to the app
**Status:** Closed | **Priority:** Medium | **Created:** November 3, 2022  
**Description:** Mobile app implementation of ORP calibration screens. Complete with two-point calibration workflow.

#### [GDV1-57](https://growdirector.atlassian.net/browse/GDV1-57) - Add DO calibration to the app
**Status:** Closed | **Priority:** Medium | **Created:** November 3, 2022  
**Description:** Mobile app implementation of DO calibration screens. Complete with two-point calibration workflow.

#### [GDV1-56](https://growdirector.atlassian.net/browse/GDV1-56) - Add EC calibration to the app
**Status:** Closed | **Priority:** High | **Created:** November 3, 2022  
**Description:** Mobile app implementation of EC calibration screens.  
**Comment:** "Need to apply design" - UI implementation completed per design specifications.

#### [GDV1-55](https://growdirector.atlassian.net/browse/GDV1-55) - Add pH calibration to the app
**Status:** Closed | **Priority:** High | **Created:** November 3, 2022  
**Description:** Mobile app implementation of pH calibration screens with validation ranges and error handling.

#### [GDV1-53](https://growdirector.atlassian.net/browse/GDV1-53) - Add backend Calibration Method for pH  
#### [GDV1-52](https://growdirector.atlassian.net/browse/GDV1-52) - Add backend Calibration Method for EC  
#### [GDV1-51](https://growdirector.atlassian.net/browse/GDV1-51) - Add backend Calibration Method for DO  
#### [GDV1-50](https://growdirector.atlassian.net/browse/GDV1-50) - Add backend Calibration Method for ORP  
**Status:** All Closed | **Priority:** Medium | **Created:** November 2, 2022  
**Description:** Backend API implementation for calibration methods across all sensor types. Complete REST API with validation, error handling, and calibration history storage.

---

### [GDV1-658](https://growdirector.atlassian.net/browse/GDV1-658) - DimmerDirector

**Status:** In Progress | **Priority:** Medium | **Created:** May 3, 2023

**Summary:**  
LED lighting control system enabling "tailored schedule and brightness of 100 LED lights per controller" with manual 0-100 voltage level control. Features MAIN channel activation (AUX disabled), Sunrise/Sunset simulation with 30-minute transitions, PAR sensor integration for light optimization, and Overheat prevention rules. Production module type 'dimmer' with connectorType 'dimmer'. Known issue: cross-subnet sensor compatibility (GDV1-2518). Recent improvements include notification system when disabling rules during Sunrise/Sunset periods (fixed in App 1.11.10).

**Business Context:**  
- Core feature for commercial greenhouse LED management
- Critical for DLI (Daily Light Integral) optimization with PAR sensors
- Sunrise/Sunset automation reduces labor and improves plant growth cycles
- Overheat prevention protects expensive LED infrastructure
- Manual override enables grower adjustments for specific crop needs
- UI/UX improvements driven by user feedback on notification clarity

**Technical Details:**
- **Module Type:** `dimmer` with `connectorType: dimmer`
- **Channels:** MAIN channel active, AUX channel disabled in production
- **Control Range:** 0-100 voltage level (manual control)
- **Automation Features:**
  - Sunrise/Sunset rules with 30-minute transition periods
  - PAR sensor integration for light-based control
  - Time-based scheduling with weekly patterns
  - Overheat prevention with temperature sensor triggers
- **Firmware:** Integrated with module firmware for real-time control
- **Design:** [Figma - Set Device Rules](https://www.figma.com/file/3t9VF3uun0CAPCrsdck9XT/Set-Device-Rules)
- **Known Issue:** Overheat prevention fails when using sensors from different WiFi subnet (GDV1-2518)

**Related Tickets (8):**

#### [GDV1-2628](https://growdirector.atlassian.net/browse/GDV1-2628) - There is no message when the dimmer rule is turning off
**Status:** Closed | **Priority:** High | **Created:** February 19, 2025  
**Problem:** No notification displayed when user disables dimmer rule during Sunrise/Sunset period, causing confusion about rule status.

**Comments (5 total):**
1. **Yuri (Feb 19):** Attached video demonstrating missing notification issue
2. **Alexander (Feb 20):** "Linked to GDV1-2352. If there is a rule for the Dimmer that starts working according to the time of sunrise or sunset, then when it is OFF, notify the user about this. For example, a notification is attached to the picture with an arrow"
3. **Alexander (Mar 5):** "Added notification in App 1.11.10: Shows sunrise/sunset time when rule is disabled during that period"
4. **Yuri (Mar 8):** Attached confirmation video showing notification working correctly after fix

**Status:** Fixed in App 1.11.10, closed March 8, 2025.

#### [GDV1-2617](https://growdirector.atlassian.net/browse/GDV1-2617) - Time of sunrise looks like an error notification
**Status:** To Do | **Priority:** Medium | **Created:** February 14, 2025  
**Problem:** UI/UX issue where sunrise time notification styling makes it appear as an error message rather than informational display. Needs design refinement for clarity.

#### [GDV1-2616](https://growdirector.atlassian.net/browse/GDV1-2616) - The rule Sunrise is called Sunset and error of sync on backend
**Status:** Closed | **Priority:** Medium | **Created:** February 14, 2025  
**Problem:** Synchronization error between mobile app and backend causing Sunrise rules to display as Sunset, and vice versa.  
**Solution (Comment):** "please test on 1.11.31" - fixed in App version 1.11.31.

#### [GDV1-2518](https://growdirector.atlassian.net/browse/GDV1-2518) - Malfunction the work of the Dimmer rule with Overheat prevention
**Status:** In Progress | **Priority:** Medium | **Created:** January 15, 2025  
**Problem:** Overheat prevention rule doesn't work when temperature sensor is on different WiFi subnet than dimmer module.  
**Description:** Sensor becomes unreachable for rule triggering when not on same local network. Requires network architecture investigation for cross-subnet MQTT communication.

#### [GDV1-2467](https://growdirector.atlassian.net/browse/GDV1-2467) - Malfunction of the module Dimmer, no possibility to set rules
**Status:** Closed | **Priority:** Medium | **Created:** December 19, 2024  
**Problem:** Dimmer rules configuration failing in production environment.

**Comments (16 total):**
1. **Marat (Dec 19):** Initial bug report with module UID and environment details
2. **Alexander (Dec 20-23):** Multiple debugging sessions with log analysis, identified MQTT command synchronization issue
3. **Sergei (Dec 24):** Database query showing inconsistent rule state between app and backend
4. **Alexander (Dec 27):** "Fixed firmware handling of rule commands. Deploying firmware 1.11.20"
5. **Yuri (Dec 30):** Testing confirmation on dev environment
6. **Marat (Jan 2):** Confirmed working on production after firmware update

**Status:** Resolved through firmware update, closed January 5, 2025.

#### [GDV1-1466](https://growdirector.atlassian.net/browse/GDV1-1466) - Dimmer bug with startDayTime when adding a rule
**Status:** Closed | **Priority:** High | **Created:** December 20, 2023  
**Problem:** API returns field with incorrect casing: `startDayTime` instead of `startdaytime`, causing mobile app parsing errors.

**Comments (10 total):**
1. **Marat (Dec 20):** "Please look, API return startDayTime (camelCase) but app expects startdaytime (lowercase)"
2. **Sergei (Dec 21):** "You mean API returns startDayTime and that's incorrect? Our API should be case-insensitive for JSON deserialization"
3. **Alexander (Dec 22):** "Let's standardize on camelCase for all API responses. I'll update backend"
4. **Marat (Dec 23):** "OK, but mobile needs to handle both for backward compatibility"
5. **Sergei (Dec 27):** "Fixed in API version 2.3.5 - now consistently returns camelCase"

**Status:** Fixed through API standardization, closed December 28, 2023.

#### [GDV1-1459](https://growdirector.atlassian.net/browse/GDV1-1459) - When trying to set a rule, check if the device is checked for the connector type dimer
**Status:** To Do | **Priority:** Medium | **Created:** December 14, 2023  
**Description:** Validation check needed: ensure only devices with `connectorType: 'dimmer'` can have dimmer rules assigned. Prevent rule assignment errors on incorrect device types.

#### [GDV1-659](https://growdirector.atlassian.net/browse/GDV1-659) - Check if DimmerDirector satisfies the specification
**Status:** Closed | **Priority:** Medium | **Created:** May 4, 2023  
**Description:** Verification ticket for production readiness and specification compliance.  
**Notes:** "AUX channel is disabled, only MAIN channel works in production. This is by design per specification."

#### [GDV1-657](https://growdirector.atlassian.net/browse/GDV1-657) - Describe DimmerDirector specification
**Status:** Closed | **Priority:** Medium | **Created:** May 3, 2023  
**Description:** Complete specification documentation for DimmerDirector feature.

**Comments (4 total):**
1. **Noam (May 3):** "Specification: Ability to tailor a schedule and brightness of 100 LED lights per controller"
2. **Noam (May 4):** Additional details:
   - Control range: 0-100 voltage level
   - Sunrise/Sunset transitions: 30 minutes
   - PAR sensor integration for feedback loop
   - Overheat prevention required
3. **Alexander (May 5):** "Module type will be 'dimmer', connectorType also 'dimmer'"
4. **Arsen (May 6):** "Contact for technical questions: [contact info provided]"

**Status:** Specification approved and documented, closed May 7, 2023.

#### [GDV1-558](https://growdirector.atlassian.net/browse/GDV1-558) - Firmware design for dimmer support
**Status:** Closed | **Priority:** Medium | **Created:** April 11, 2023  
**Description:** Firmware architecture design for dimmer hardware integration.

**Comments (3 total):**
1. **Dima M (Apr 11):** "Is there any additional activity for dimmer support beyond what's in the module firmware?"
2. **Arsen (Apr 12):** Detailed firmware status in Russian describing module implementation approach with manual control and rule support
3. **Alexander (Apr 13):** Shared Figma design link: https://www.figma.com/file/3t9VF3uun0CAPCrsdck9XT/Set-Device-Rules

**Status:** Design approved and implemented, closed April 15, 2023.

---

## All Epics (Alphabetical)

### Complete List of All 54 Epics

#### 1. [GDV1-827](https://growdirector.atlassian.net/browse/GDV1-827) - Admin Panel - Statistics, Analytics & Management
**Status:** In Progress | **Priority:** Medium | **Created:** August 22, 2023

**Description:** Production admin portal (panel.growdirector.com) for system management, user/module statistics tracking, firmware CRUD operations, AI Reports configuration with timezone/unit/language support, custom channel management, and test account filtering. Two design-focused tickets remain for enhanced analytics with map visualization.

**See detailed analysis:** [Admin Panel Section](#gdv1-827---admin-panel---statistics-analytics--management)

---

#### 2. [GDV1-294](https://growdirector.atlassian.net/browse/GDV1-294) - Agri Notes [GrowBook / GrowLog]
**Status:** In Progress | **Priority:** Medium | **Created:** 2022-11-27

**Description:** GrowBook - integration of sensor data from devices and basic ERP for growers including:

1. Integrated Pest Management (IPM) System with image recognition
2. Crop Rotation Planning
3. Task Management for labor force
4. Advanced Analytics and Reporting on crop performance
5. Equipment Maintenance Scheduling
6. Crop Variety Selection based on local conditions
7. Yield prediction software
8. Training software with ChatGPT integration

**Links:**
- [Figma Design](https://www.figma.com/design/3YvLwmLEUEzJ7gmglAiViO/Grow-Book)
- [Swagger API](http://78.47.183.121:8094/swagger/index.html)

---

#### 3. [GDV1-900](https://growdirector.atlassian.net/browse/GDV1-900) - Alarm Service
**Status:** Closed | **Priority:** Medium | **Created:** 2023-09-15

Alarm and alert system implementation.

---

#### 4. [GDV1-538](https://growdirector.atlassian.net/browse/GDV1-538) - ATE FW Support (Test button)
**Status:** Closed | **Priority:** Medium | **Created:** 2023-03-31

**Description:** Complete work on ATE support. Definition of Done: ATE support integrated into last official release.

Led by Dima Moshkovich with Pavel Stankevich allocated for the task.

---

#### 5. [GDV1-874](https://growdirector.atlassian.net/browse/GDV1-874) - Bug-fixing 1.4.0
**Status:** Closed | **Priority:** Medium | **Created:** 2023-09-03

Bug fixes for release 1.4.0

---

#### 6. [GDV1-873](https://growdirector.atlassian.net/browse/GDV1-873) - Bug-fixing 1.3.0
**Status:** Closed | **Priority:** Medium | **Created:** 2023-09-03

Bug fixes for release 1.3.0

---

#### 7. [GDV1-2451](https://growdirector.atlassian.net/browse/GDV1-2451) - Bug-fixing 1.11.0
**Status:** In Progress | **Priority:** Medium | **Created:** 2024-12-24

Bug fixes for release 1.11.0

---

#### 8. [GDV1-2612](https://growdirector.atlassian.net/browse/GDV1-2612) - Bug-fixing 1.12
**Status:** Done | **Priority:** Medium | **Created:** 2025-03-01

Bug fixes for release 1.12

---

#### 9. [GDV1-2787](https://growdirector.atlassian.net/browse/GDV1-2787) - Bug-fixing 1.13
**Status:** Closed | **Priority:** Medium | **Created:** 2025-05-08

Bug fixes for release 1.13

---

#### 10. [GDV1-2902](https://growdirector.atlassian.net/browse/GDV1-2902) - Bug-fixing 1.14
**Status:** Closed | **Priority:** Medium | **Created:** 2025-07-15

Bug fixes for release 1.14

---

#### 11. [GDV1-3017](https://growdirector.atlassian.net/browse/GDV1-3017) - Bug-fixing 1.15
**Status:** Done | **Priority:** Medium | **Created:** 2025-09-17

Bug fixes for release 1.15

---

#### 12. [GDV1-3136](https://growdirector.atlassian.net/browse/GDV1-3136) - Bug-fixing 1.16
**Status:** To Do | **Priority:** Medium | **Created:** 2025-12-02

Bug fixes for release 1.16

---

#### 13. [GDV1-628](https://growdirector.atlassian.net/browse/GDV1-628) - Bug-fixing S9
**Status:** Closed | **Priority:** Medium | **Created:** 2023-06-13

Bug fixes for Sprint 9

---

#### 14. [GDV1-126](https://growdirector.atlassian.net/browse/GDV1-126) - Climate
**Status:** Closed | **Priority:** Medium | **Created:** 2022-10-05

Climate monitoring and management features.

---

#### 15. [GDV1-190](https://growdirector.atlassian.net/browse/GDV1-190) - Climate history
**Status:** Closed | **Priority:** Medium | **Created:** 2022-10-31

Historical climate data tracking and visualization.

---

#### 16. [GDV1-152](https://growdirector.atlassian.net/browse/GDV1-152) - Climate section
**Status:** Closed | **Priority:** Medium | **Created:** 2022-10-12

Climate section in the app.

---

#### 17. [GDV1-2362](https://growdirector.atlassian.net/browse/GDV1-2362) - Dashboard
**Status:** To Do | **Priority:** Low | **Created:** 2024-11-08

Main dashboard development.

---

#### 18. [GDV1-512](https://growdirector.atlassian.net/browse/GDV1-512) - Debug Logs: Infrastructure & Usage
**Status:** Closed | **Priority:** Medium | **Created:** 2023-03-10

**Description:** Provide functionality to report and collect logs from App and firmware for troubleshooting and debugging.

---

#### 19. [GDV1-181](https://growdirector.atlassian.net/browse/GDV1-181) - Design system
**Status:** Closed | **Priority:** Medium | **Created:** 2022-10-27

Design system development and implementation.

---

#### 20. [GDV1-885](https://growdirector.atlassian.net/browse/GDV1-885) - Device operation history
**Status:** Done | **Priority:** Medium | **Created:** 2023-09-11

Device operation history tracking.

---

#### 21. [GDV1-201](https://growdirector.atlassian.net/browse/GDV1-201) - Devices
**Status:** Closed | **Priority:** Medium | **Created:** 2022-11-02

Device management features.

---

#### 22. [GDV1-106](https://growdirector.atlassian.net/browse/GDV1-106) - Dimmer
**Status:** Closed | **Priority:** Medium | **Created:** 2022-09-28

Dimmer device support.

---

#### 23. [GDV1-658](https://growdirector.atlassian.net/browse/GDV1-658) - DimmerDirector
**Status:** In Progress | **Priority:** Medium | **Created:** May 3, 2023

**Description:** LED lighting control system enabling "tailored schedule and brightness of 100 LED lights per controller" with manual 0-100 voltage level control, MAIN channel activation (AUX disabled), Sunrise/Sunset simulation with 30-minute transitions, PAR sensor integration, and Overheat prevention. Module type 'dimmer'. Known issue: cross-subnet sensor compatibility (GDV1-2518). Recent improvements include notification system for rule disable during Sunrise/Sunset (App 1.11.10).

**See detailed analysis:** [DimmerDirector Section](#gdv1-658---dimmerdirector)

---

#### 24. [GDV1-3230](https://growdirector.atlassian.net/browse/GDV1-3230) - DLI
**Status:** To Do | **Priority:** Medium | **Created:** 2026-01-29

**Description:** Daily Light Integral (DLI) implementation as virtual sensor. Includes:

**Phase B1:** DLI Calculation Logic (Backend)
- DLI virtual sensor type definition
- DLI calculator implementation with PPFD integration
- API for DLI readings & history
- Crop targets data source (Lettuce, Basil, Tomatoes, etc.)

**Phase B2:** Firmware Support (if needed)
- Sensor data granularity validation
- Optional local DLI computation for offline mode
- Exposing DLI as sensor channel

**Phase B3:** Frontend DLI UI & Configuration
- DLI virtual sensor creation wizard
- DLI box next to Lux/PAR in SensorDirector
- DLI detail view with progress bar and charts

**Phase B4:** Rules for Devices Using DLI
- Rule types using DLI (daily target check, realtime upper-bound)
- Device actions integration with DimmerDirector
- Rule editor DLI integration

**Phase B5:** Reporting & QA
- Historical reporting
- Edge cases testing
- Integration tests

---

#### 25. [GDV1-661](https://growdirector.atlassian.net/browse/GDV1-661) - Documentation
**Status:** To Do | **Priority:** Medium | **Created:** 2023-06-20

Project documentation development and maintenance.

---

#### 26. [GDV1-904](https://growdirector.atlassian.net/browse/GDV1-904) - Add & Fix links to documentation
**Status:** To Do | **Priority:** Medium | **Created:** 2023-09-16

Add and fix documentation links throughout the application.

---

#### 27. [GDV1-399](https://growdirector.atlassian.net/browse/GDV1-399) - Export data
**Status:** To Do | **Priority:** Medium | **Created:** 2022-12-22

Data export functionality (Excel, CSV, etc.).

---

#### 28. [GDV1-116](https://growdirector.atlassian.net/browse/GDV1-116) - Filters
**Status:** Closed | **Priority:** Medium | **Created:** 2022-09-29

Filter functionality across different sections.

---

#### 29. [GDV1-528](https://growdirector.atlassian.net/browse/GDV1-528) - Firmware Upgrade Phase 1 - Manual Upgrade
**Status:** Closed | **Priority:** Medium | **Created:** 2023-03-27

[Documentation](https://docs.google.com/document/d/1A2PdIJKjNyZyAc6MkcMl4h6IOA3kUzVOJUdcic6IBHc/edit)

First phase of firmware upgrade functionality - manual upgrade process.

---

#### 30. [GDV1-899](https://growdirector.atlassian.net/browse/GDV1-899) - Firmware uploading (module update)
**Status:** Closed | **Priority:** Medium | **Created:** 2023-09-15

Firmware upload and module update mechanism.

---

#### 29. [GDV1-936](https://growdirector.atlassian.net/browse/GDV1-936) - Improve module connection
**Status:** Closed | **Priority:** Medium | **Created:** 2023-09-28

Improvements to module connection process.

---

#### 32. [GDV1-2567](https://growdirector.atlassian.net/browse/GDV1-2567) - Integrations
**Status:** To Do | **Priority:** Medium | **Created:** 2025-02-19

Third-party integrations and API connections.

---

#### 33. [GDV1-554](https://growdirector.atlassian.net/browse/GDV1-554) - Manual Connection: Onboarding instruction
**Status:** Closed | **Priority:** Medium | **Created:** 2023-05-02

Manual connection onboarding and instructions.

---

#### 34. [GDV1-2842](https://growdirector.atlassian.net/browse/GDV1-2842) - Misting System
**Status:** Done | **Priority:** Medium | **Created:** 2025-06-04

Misting system device type support.

---

#### 35. [GDV1-135](https://growdirector.atlassian.net/browse/GDV1-135) - Modules
**Status:** Closed | **Priority:** Medium | **Created:** 2022-10-06

Module management features.

---

#### 36. [GDV1-103](https://growdirector.atlassian.net/browse/GDV1-103) - Notifications
**Status:** To Do | **Priority:** Medium | **Created:** 2022-09-28

Push notifications, in-app notifications, email and SMS notifications.

---

#### 37. [GDV1-903](https://growdirector.atlassian.net/browse/GDV1-903) - Offline Mode (Modules & Sensors Data)
**Status:** In Progress | **Priority:** Highest | **Created:** September 16, 2023

**Description:** Comprehensive offline functionality enabling autonomous module operation during internet outages. Implements sparse storage algorithm (5min→30min→1hr) for sensor data preservation, MQTT history topics (sensor/device/analog_control) for post-reconnection sync, 4 log types for diagnostics, WasOffline flag for backend tracking, and different color display for offline data. Critical for remote growing facility reliability.

**See detailed analysis:** [Offline Mode Section](#gdv1-903---offline-mode-modules--sensors-data)

---

#### 38. [GDV1-2438](https://growdirector.atlassian.net/browse/GDV1-2438) - Proactive Shutdown
**Status:** Closed | **Priority:** Medium | **Created:** 2024-12-13

**Description:** Automatically shut down devices to prevent harm when they exceed operation thresholds without achieving expected results. Specifically when sensor parameters show no movement towards desired set-point.

---

#### 39. [GDV1-2642](https://growdirector.atlassian.net/browse/GDV1-2642) - Recipes
**Status:** To Do | **Priority:** Low | **Created:** 2025-03-11

Recipe/template system for common automation scenarios.

---

#### 40. [GDV1-902](https://growdirector.atlassian.net/browse/GDV1-902) - Reconnect module problem
**Status:** Closed | **Priority:** Medium | **Created:** 2023-09-16

Fix module reconnection issues.

---

#### 41. [GDV1-511](https://growdirector.atlassian.net/browse/GDV1-511) - Redesign
**Status:** To Do | **Priority:** Medium | **Created:** 2023-03-10

General UI/UX redesign.

---

#### 42. [GDV1-609](https://growdirector.atlassian.net/browse/GDV1-609) - Redesign Device & Rules
**Status:** Closed | **Priority:** Medium | **Created:** 2023-06-08

Redesign of device and rules interfaces.

---

#### 43. [GDV1-895](https://growdirector.atlassian.net/browse/GDV1-895) - Release notes
**Status:** Closed | **Priority:** Medium | **Created:** 2023-09-13

Release notes generation and management.

---

#### 44. [GDV1-2840](https://growdirector.atlassian.net/browse/GDV1-2840) - Rooms [zones]
**Status:** Closed | **Priority:** Medium | **Created:** 2025-06-04

Room/zone organization for devices and sensors.

---

#### 45. [GDV1-2687](https://growdirector.atlassian.net/browse/GDV1-2687) - RS-485
**Status:** To Do | **Priority:** Medium | **Created:** 2025-04-03

RS-485 communication protocol support.

---

#### 46. [GDV1-3133](https://growdirector.atlassian.net/browse/GDV1-3133) - SaaS
**Status:** In Progress | **Priority:** Medium | **Created:** 2025-12-02

SaaS subscription model implementation.

---

#### 47. [GDV1-2496](https://growdirector.atlassian.net/browse/GDV1-2496) - Sales AI
**Status:** To Do | **Priority:** Medium | **Created:** 2025-01-09

**Description:** Chatbot for sales and consulting purposes for growdirector.com website.

---

#### 48. [GDV1-49](https://growdirector.atlassian.net/browse/GDV1-49) - Sensor calibration
**Status:** In Progress | **Priority:** Medium | **Created:** October 28, 2022

**Description:** Comprehensive two-point calibration system for pH, EC, DO, ORP, and Soil moisture sensors. Validation ranges refined through field testing (pH: 1500-2600/2600-3700, EC: 180-750/1100-3400). Features out-of-range notification system, Admin API flags (FirstRawValueIsOutOfRange/SecondRawValueIsOutOfRange), calibration history tracking, and 120-second calibration duration per point.

**See detailed analysis:** [Sensor Calibration Section](#gdv1-49---sensor-calibration)

---

#### 49. [GDV1-868](https://growdirector.atlassian.net/browse/GDV1-868) - Sensor connection
**Status:** In Progress | **Priority:** Medium | **Created:** 2023-09-01

**Description:** Complete sensor connection workflow including:

**Variant 1:** API-based sensor scanning and connection
- Sensor scanning via MQTT commands
- Pin selection and sensor type identification  
- Validation for digital and analog sensors
- Support for pH/ORP/DO sensors with specific validation

**Variant 2 (Simplified):** Local sensor connection
- Direct communication with module in same network
- Faster response without MQTT delays

Key features:
- Sensor must be added to system by user
- Sensor state tracking: installed | notInstalled | disconnected
- Digital sensors: automatic detection and addition prompt
- Analog sensors: type selection and validation
- Cannot change sensor type - must delete and re-add

---

#### 50. [GDV1-553](https://growdirector.atlassian.net/browse/GDV1-553) - Sensors: DO and ORP support
**Status:** In Test | **Priority:** Medium | **Created:** 2023-05-02

**Description:** DO (Dissolved Oxygen) and ORP (Oxidation-Reduction Potential) sensors support.

---

#### 51. [GDV1-144](https://growdirector.atlassian.net/browse/GDV1-144) - Sensor's page
**Status:** Closed | **Priority:** Medium | **Created:** 2022-10-10

Sensor details and management page.

---

#### 52. [GDV1-948](https://growdirector.atlassian.net/browse/GDV1-948) - Add Sensors Soil / LUX / PAR
**Status:** In Progress | **Priority:** Medium | **Created:** December 2, 2023

**Description:** Sensor expansion initiative for PAR (Apogee SQ-512-SS), LUX (RIKA RK-210-01), and Soil moisture (RIKA RK510-01) sensors. API endpoint `/api/v1/Sensor/{uid}/set_type` with MQTT `SensorTypeCommandNotification`. Calibration duration 120 seconds. Critical issue: soil moisture instability (79%-96% fluctuation) requiring 5-minute threshold delay solution. Firmware versions: 1.6.16 (initial), 1.6.31, 1.6.32 (stability fixes).

**See detailed analysis:** [Add Sensors Section](#gdv1-948---add-sensors-soil--lux--par)

---

#### 53. [GDV1-178](https://growdirector.atlassian.net/browse/GDV1-178) - Settings
**Status:** In Progress | **Priority:** Medium | **Created:** 2022-10-27

Settings page and configuration options.

---

#### 54. [GDV1-80](https://growdirector.atlassian.net/browse/GDV1-80) - Settings section
**Status:** Closed | **Priority:** Medium | **Created:** 2022-09-14

Settings section implementation.

---

#### 55. [GDV1-2905](https://growdirector.atlassian.net/browse/GDV1-2905) - Summer/Winter Time [TimeZone]
**Status:** Closed | **Priority:** Medium | **Created:** 2025-07-16

Time zone support and daylight saving time handling.

---

#### 56. [GDV1-2669](https://growdirector.atlassian.net/browse/GDV1-2669) - Technical debt
**Status:** To Do | **Priority:** Medium | **Created:** 2025-03-27

Technical debt tracking and resolution.

---

#### 57. [GDV1-2577](https://growdirector.atlassian.net/browse/GDV1-2577) - Trigger Delay
**Status:** Closed | **Priority:** High | **Created:** 2025-02-20

**Description:** New feature to set time during which sensor value must satisfy triggering condition before rule activates.

Available for Sensor Rule and Step Rule types.

---

#### 58. [GDV1-107](https://growdirector.atlassian.net/browse/GDV1-107) - Tutorial
**Status:** Closed | **Priority:** Medium | **Created:** 2022-09-28

In-app tutorial and onboarding.

---

#### 59. [GDV1-906](https://growdirector.atlassian.net/browse/GDV1-906) - User Settings
**Status:** In Progress | **Priority:** Medium | **Created:** 2023-09-17

User-specific settings and preferences.

---

#### 60. [GDV1-92](https://growdirector.atlassian.net/browse/GDV1-92) - Version of API and client in app
**Status:** Closed | **Priority:** Medium | **Created:** 2022-09-23

Display API and client version information in app.

---

#### 61. [GDV1-3229](https://growdirector.atlassian.net/browse/GDV1-3229) - Virtual Sensors
**Status:** To Do | **Priority:** Medium | **Created:** 2026-01-29

**Description:** Generic Virtual Sensors Framework where DLI is one possible type. Includes:

**Phase A1:** Architecture & Data Model
- Virtual Sensor domain model definition
- API contract for virtual sensors
- Persistence & indexing strategy
- Firmware responsibility clarification

**Phase A2:** Virtual Sensors Engine (Backend)
- Generic computation engine
- Type-specific logic interfaces (AVERAGE, MIN, MAX, ROLLING_AVERAGE)
- Integration with rules engine

**Phase A3:** Frontend Virtual Sensors Management UI
- Virtual sensors list management
- Create/Edit wizard
- Integration into rules UI

Other virtual sensor types: hourly average, rolling average, min/max, custom formulas, etc.

---

#### 62. [GDV1-2591](https://growdirector.atlassian.net/browse/GDV1-2591) - Workflow Automation & Sequencing
**Status:** In Progress | **Priority:** Highest | **Created:** 2025-02-24

**Description:** Automated action plans for devices like fans, heaters, curtains/vents, CO₂ valves, pumps. Each plan follows rules set for each device ensuring smooth operations.

**Key Features:**
- Workflow configuration limitations
- Available only in Desktop version
- Only one workflow per module
- Cannot combine with rules (exclusive)
- Supports Dry, Socket, and Hydro modules

**Related:**
- [Functional Specification](https://growdirector.atlassian.net/wiki/x/AoD9Mg)
- Data format: [GDV1-3174](https://growdirector.atlassian.net/browse/GDV1-3174)

This is base for company leverage over competitors and aligning with major players. Long-term epic with multiple phases.

---

## Epic Details with Tickets

## Epic Details with Tickets

For detailed epic information, see the [All Epics (Alphabetical)](#all-epics-alphabetical) section above.

### Major Feature Epics

**Workflow & Automation:**
- [GDV1-2591](https://growdirector.atlassian.net/browse/GDV1-2591) - Workflow Automation & Sequencing (In Progress, Highest Priority)
- [GDV1-2577](https://growdirector.atlassian.net/browse/GDV1-2577) - Trigger Delay (Closed, High Priority)
- [GDV1-2438](https://growdirector.atlassian.net/browse/GDV1-2438) - Proactive Shutdown (Closed)

**Virtual Sensors & DLI:**
- [GDV1-3229](https://growdirector.atlassian.net/browse/GDV1-3229) - Virtual Sensors Framework (To Do)
- [GDV1-3230](https://growdirector.atlassian.net/browse/GDV1-3230) - DLI Implementation (To Do)

**Business & SaaS:**
- [GDV1-3133](https://growdirector.atlassian.net/browse/GDV1-3133) - SaaS Subscriptions (In Progress)
- [GDV1-2496](https://growdirector.atlassian.net/browse/GDV1-2496) - Sales AI Chatbot (To Do)

**Hardware & Sensors:**
- [GDV1-868](https://growdirector.atlassian.net/browse/GDV1-868) - Sensor Connection (In Progress)
- [GDV1-553](https://growdirector.atlassian.net/browse/GDV1-553) - DO and ORP Support (In Test)
- [GDV1-948](https://growdirector.atlassian.net/browse/GDV1-948) - Soil moisture, LUX, PAR Sensors (In Progress)
- [GDV1-49](https://growdirector.atlassian.net/browse/GDV1-49) - Sensor Calibration (In Progress)
- [GDV1-2842](https://growdirector.atlassian.net/browse/GDV1-2842) - Misting System (Done)
- [GDV1-658](https://growdirector.atlassian.net/browse/GDV1-658) - DimmerDirector (In Progress)
- [GDV1-2687](https://growdirector.atlassian.net/browse/GDV1-2687) - RS-485 Support (To Do)

**System & Infrastructure:**
- [GDV1-903](https://growdirector.atlassian.net/browse/GDV1-903) - Offline Mode (In Progress, Highest Priority)
- [GDV1-2905](https://growdirector.atlassian.net/browse/GDV1-2905) - Time Zone & DST (Closed)
- [GDV1-899](https://growdirector.atlassian.net/browse/GDV1-899) - Firmware Uploading (Closed)
- [GDV1-936](https://growdirector.atlassian.net/browse/GDV1-936) - Improve Module Connection (Closed)
- [GDV1-512](https://growdirector.atlassian.net/browse/GDV1-512) - Debug Logs (Closed)

**UI/UX & Features:**
- [GDV1-2840](https://growdirector.atlassian.net/browse/GDV1-2840) - Rooms/Zones (Closed)
- [GDV1-885](https://growdirector.atlassian.net/browse/GDV1-885) - Device Operation History (Done)
- [GDV1-294](https://growdirector.atlassian.net/browse/GDV1-294) - Agri Notes / GrowBook (In Progress)
- [GDV1-2362](https://growdirector.atlassian.net/browse/GDV1-2362) - Dashboard (To Do)
- [GDV1-103](https://growdirector.atlassian.net/browse/GDV1-103) - Notifications (To Do)
- [GDV1-399](https://growdirector.atlassian.net/browse/GDV1-399) - Export Data (To Do)

**Bug Fixing Epics:**
- [GDV1-3136](https://growdirector.atlassian.net/browse/GDV1-3136) - Bug-fixing 1.16 (To Do)
- [GDV1-3017](https://growdirector.atlassian.net/browse/GDV1-3017) - Bug-fixing 1.15 (Done)
- [GDV1-2902](https://growdirector.atlassian.net/browse/GDV1-2902) - Bug-fixing 1.14 (Closed)
- [GDV1-2787](https://growdirector.atlassian.net/browse/GDV1-2787) - Bug-fixing 1.13 (Closed)
- [GDV1-2612](https://growdirector.atlassian.net/browse/GDV1-2612) - Bug-fixing 1.12 (Done)
- [GDV1-2451](https://growdirector.atlassian.net/browse/GDV1-2451) - Bug-fixing 1.11.0 (In Progress)

---

## Quick Links

### Jira Project Links

- **Main Board:** [GDV1 Jira Board](https://growdirector.atlassian.net/jira/software/projects/GDV1/board)
- **Backlog:** [GDV1 Backlog](https://growdirector.atlassian.net/jira/software/projects/GDV1/backlog)
- **Active Sprints:** [Current Sprints](https://growdirector.atlassian.net/jira/software/projects/GDV1/sprints)
- **Create Issue:** [New Ticket](https://growdirector.atlassian.net/secure/CreateIssue!default.jspa?pid=10000)

### Documentation Links

- **Confluence Wiki:** [GDS Space](https://growdirector.atlassian.net/wiki/spaces/GDS)
- **Architecture & Solutions:** [Technical Architecture](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/278757448)
- **Development Workflow:** [Dev & Release Process](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/291667969)
- **Team Onboarding:** [Onboarding Guide](https://growdirector.atlassian.net/wiki/spaces/GDS/pages/245956616)

### External Resources

- **Figma Designs:** [Design Files](https://www.figma.com/files/team/1146793506989825524/GD)
- **Architecture Diagrams:** [Diagrams.net](https://app.diagrams.net/#G1EZmzXrTEai_IJQBIeNPKIxb5cMUWzuO6)

---

## Ticket Naming Convention

All tickets in the GrowDirector project follow this naming convention:

**Format:** `GDV1-XXX`

Where:
- `GDV1` = Project code (GrowDirector v1/v3)
- `XXX` = Sequential ticket number

**Example Tickets:**
- [GDV1-553](https://growdirector.atlassian.net/browse/GDV1-553) - Epic: Workflow Automation
- [GDV1-778](https://growdirector.atlassian.net/browse/GDV1-778) - Bug: Fix Hints position and size
- [GDV1-641](https://growdirector.atlassian.net/browse/GDV1-641) - Feature: Removing a sensor from the system
- [GDV1-862](https://growdirector.atlassian.net/browse/GDV1-862) - Feature: Add custom sorting for devices

---

## Notes

**Data Source:** This document is compiled from Confluence documentation and release notes. Some specific ticket numbers may not be available in the public documentation, but epic goals and features are documented based on release notes and functional specifications.

**Status:** The information in this document is current as of January 31, 2026. For the most up-to-date information, please refer to the [Jira board](https://growdirector.atlassian.net/jira/software/projects/GDV1).

**Updates:** This document should be updated regularly as new epics are created and tickets are completed.

---

**Last Updated:** January 31, 2026  
**Maintained By:** GrowDirector Development Team  
**Related Document:** [Confluence Wiki Structure](./confluence-wiki-structure.md)
