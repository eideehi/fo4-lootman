# LootMan Native Hook Address Review Bundle

Binary version: Fallout4 1.11.240
Manifest target runtime: Fallout4 1.11.240

## Bundle Files
- Manifest copy: tools/native-hooks/reports/fallout4-1.11.240/manifest.json
- Candidate RVAs: tools/native-hooks/reports/fallout4-1.11.240/candidate-rvas.json
- Current hook source slice: tools/native-hooks/reports/fallout4-1.11.240/papyrus_lootman_hooks.slice.cpp

## Candidate RVAs
- encounter_zone.load_change_cell_before_zone_reset (call_site_rva, proven): encounter-zone.reset-suppression.cell-before-reset=0x4D26F4
- encounter_zone.reset_elapsed_from_detach_time (function_rva, automated): value=0x4D3150
- workshop_shared_container.workshop_caravan_keyword_global (global_rva, automated): value=0x30F7AB8
- workshop_material.current_workshop_handle_global (global_rva, automated): value=0x30F7698
- workshop_shared_container.populate_linked_workshop_container (call_site_rva, proven): workshop-shared-container.populate-linked.primary=0x3922A8, workshop-shared-container.populate-linked.workbench=0xB28966, workshop-shared-container.populate-linked.menu=0x10895E6
- workshop_material.rebuild_workshop_supply (call_site_rva, proven): workshop-material.rebuild-supply.source-a1=0xA65746, workshop-material.rebuild-supply.source-a2=0xA5F459, workshop-material.rebuild-supply.source-a3=0xA6087C, workshop-material.rebuild-supply.source-a4=0xAEF9A9
- workshop_material.component_count_helper (call_site_rva, proven): workshop-material.component-count.papyrus=0x59BF5A, workshop-material.component-count.workbench-ui=0x117550B
- workshop_material.direct_component_count (call_site_rva, proven): workshop-material.direct-component-count.source-e1=0x3BC71D, workshop-material.direct-component-count.source-e2=0x39F5AF, workshop-material.direct-component-count.source-e3=0xB32E7B, workshop-material.direct-component-count.source-e4=0xB37828, workshop-material.direct-component-count.source-e5=0xB2D13E
- workshop_material.resource_status (call_site_rva, proven): workshop-material.resource-status.source-f1=0xB2F0B0, workshop-material.resource-status.source-f2=0xB2D056
- workshop_menu.select (call_site_rva, proven): workshop-menu.select.source-a1=0xB2C69A, workshop-menu.select.source-a2=0xB2C957
- workshop_menu.availability (call_site_rva, proven): workshop-menu.availability.source-91=0xB2C65E, workshop-menu.availability.source-92=0xB2C6C7, workshop-menu.availability.source-93=0xB2C91E, workshop-menu.availability.source-94=0xB2C984, workshop-menu.availability.source-95=0xB2E9D4, workshop-menu.availability.source-96=0x397185, workshop-menu.availability.source-97=0x3B82D2, workshop-menu.availability.source-98=0x3C1BCF, workshop-menu.availability.source-99=0xB2B894, workshop-menu.availability.source-9a=0xB2D75F, workshop-menu.availability.source-9b=0xB2D891, workshop-menu.availability.source-9c=0xB2ECFF, workshop-menu.availability.source-9d=0xB3035B, workshop-menu.availability.source-9e=0xB323A5, workshop-menu.availability.source-9f=0xB330EC, workshop-menu.availability.source-a0=0xB36148, workshop-menu.availability.source-a1=0xB36651, workshop-menu.availability.source-a2=0xB37630, workshop-menu.availability.source-a3=0xB376DB
- workshop_menu.check_and_set_placement (call_site_rva, proven): workshop-menu.check-placement.source-a5=0xB2B0F7, workshop-menu.check-placement.source-a6=0xB2C6E2, workshop-menu.check-placement.source-a7=0xB2C99F, workshop-menu.check-placement.source-a8=0xB2E67E
- workshop_menu.start_placement (call_site_rva, proven): workshop-menu.start-placement.source-a3=0xB2C7DA, workshop-menu.start-placement.source-a4=0xB2CA95, workshop-menu.start-placement.source-a9=0xB2B0EF, workshop-menu.start-placement.source-aa=0xB2B954, workshop-menu.start-placement.source-ab=0xB2D900, workshop-menu.start-placement.source-ac=0xB2E676, workshop-menu.start-placement.source-ad=0xB2E8FF
- workshop_material.build_resource_check (call_site_rva, proven): workshop-material.build-resource-check.placement=0x392844, workshop-material.build-resource-check.confirm=0x399136, workshop-material.build-resource-check.consume-precheck=0x3B7F7E
- workshop_material.consume_component (call_site_rva, proven): workshop-material.consume-component.source-f3=0x399326, workshop-material.consume-component.source-f4=0x3B805A
- workshop_menu.selected_menu_node_function (function_rva, automated): value=0x389DB0
- workshop_menu.selected_row_global (global_rva, automated): value=0x30F6F18
- workshop_material.resource_status_missing_resources (constant, manual): value=2
- workshop_material.remove_components (call_site_rva, proven): workshop-material.remove-components.source-f1=0x114F009, workshop-material.remove-components.source-f2=0x114EA33
- workshop_material.object_count_papyrus (call_site_rva, proven): workshop-material.object-count.papyrus=0x5DD7B4
- workshop_material.current_workshop_object_count (call_site_rva, proven): workshop-material.object-count.current-workshop=0x59D6A8
- workshop_supply_owner.field_e0 (layout_offset, manual): value=0xE0
- workshop_supply_owner.field_e8 (layout_offset, manual): value=0xE8
- workshop_supply_owner.field_f8 (layout_offset, manual): value=0xF8
- workshop_supply_owner.field_2f8 (layout_offset, manual): value=0x2F8

## Instruction Windows
- encounter_zone.load_change_cell_before_zone_reset: 0x4D26F4
- workshop_shared_container.populate_linked_workshop_container: 0x3922A8, 0xB28966, 0x10895E6
- workshop_material.rebuild_workshop_supply: 0xA65746, 0xA5F459, 0xA6087C, 0xAEF9A9
- workshop_material.component_count_helper: 0x59BF5A, 0x117550B
- workshop_material.direct_component_count: 0x3BC71D, 0x39F5AF, 0xB32E7B, 0xB37828, 0xB2D13E
- workshop_material.resource_status: 0xB2F0B0, 0xB2D056
- workshop_menu.select: 0xB2C69A, 0xB2C957
- workshop_menu.availability: 0xB2C65E, 0xB2C6C7, 0xB2C91E, 0xB2C984, 0xB2E9D4, 0x397185, 0x3B82D2, 0x3C1BCF, 0xB2B894, 0xB2D75F, 0xB2D891, 0xB2ECFF, 0xB3035B, 0xB323A5, 0xB330EC, 0xB36148, 0xB36651, 0xB37630, 0xB376DB
- workshop_menu.check_and_set_placement: 0xB2B0F7, 0xB2C6E2, 0xB2C99F, 0xB2E67E
- workshop_menu.start_placement: 0xB2C7DA, 0xB2CA95, 0xB2B0EF, 0xB2B954, 0xB2D900, 0xB2E676, 0xB2E8FF
- workshop_material.build_resource_check: 0x392844, 0x399136, 0x3B7F7E
- workshop_material.consume_component: 0x399326, 0x3B805A
- workshop_material.remove_components: 0x114F009, 0x114EA33
- workshop_material.object_count_papyrus: 0x5DD7B4
- workshop_material.current_workshop_object_count: 0x59D6A8

## Proof Readiness
### already_proven
- encounter_zone.load_change_cell_before_zone_reset: target=0x140494E60; selectedRefs=n/a; directCalls=1/1; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_shared_container.populate_linked_workshop_container: target=0x14038A140; selectedRefs=n/a; directCalls=3/3; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.rebuild_workshop_supply: target=0x140B29480; selectedRefs=n/a; directCalls=4/4; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.component_count_helper: target=0x140507990; selectedRefs=n/a; directCalls=2/2; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.direct_component_count: target=0x140507D30; selectedRefs=n/a; directCalls=5/5; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.resource_status: target=0x140B32DA0; selectedRefs=n/a; directCalls=2/2; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_menu.select: target=0x1403970E0; selectedRefs=n/a; directCalls=2/2; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_menu.availability: target=0x140399AD0; selectedRefs=n/a; directCalls=19/19; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_menu.check_and_set_placement: target=0x140B2E940; selectedRefs=n/a; directCalls=4/4; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_menu.start_placement: target=0x140B2FF30; selectedRefs=n/a; directCalls=7/7; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.build_resource_check: target=0x14042BFF0; selectedRefs=n/a; directCalls=3/3; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.consume_component: target=0x140392340; selectedRefs=n/a; directCalls=2/2; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.remove_components: target=0x141182A90; selectedRefs=n/a; directCalls=2/2; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.object_count_papyrus: target=0x14059D690; selectedRefs=n/a; directCalls=1/1; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.current_workshop_object_count: target=0x14037E190; selectedRefs=n/a; directCalls=1/1; untriagedExtras=0; No proof refresh needed; resolver proof metadata is already present.
### not_applicable
- encounter_zone.reset_elapsed_from_detach_time: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_shared_container.workshop_caravan_keyword_global: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_material.current_workshop_handle_global: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_menu.selected_menu_node_function: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_menu.selected_row_global: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_material.resource_status_missing_resources: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_supply_owner.field_e0: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_supply_owner.field_e8: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_supply_owner.field_f8: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.
- workshop_supply_owner.field_2f8: target=none; selectedRefs=n/a; directCalls=n/a; untriagedExtras=0; No call-site proof refresh needed.

## Referenced Ghidra Reports
- tools/ghidra/reports/fallout4-1.11.240/fo4-address-library-global-refs.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-address-library-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-availability-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-availability-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-build-resource-check-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-build-resource-check-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-check-placement-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-check-placement-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-component-count-helper-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-component-count-helper-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-consume-component-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-consume-component-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-direct-component-count-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-direct-component-count-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-encounter-zone-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-encounter-zone-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-menu-select-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-menu-select-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-object-count-current-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-object-count-current-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-object-count-papyrus-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-object-count-papyrus-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-rebuild-supply-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-rebuild-supply-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-remove-components-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-remove-components-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-resource-status-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-resource-status-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-shared-container-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-shared-container-target-functions.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-start-placement-call-windows.txt
- tools/ghidra/reports/fallout4-1.11.240/fo4-start-placement-target-functions.txt
- tools/ghidra/reports/fo4-placement-resource-status-functions.txt
- tools/ghidra/reports/fo4-workbench-shared-container-callers.txt

## Manual Non-Executable Entries
- workshop_material.resource_status_missing_resources (constant): value=2; This is a semantic status value, not an executable RVA; update only if the resource status enum is re-proven.
- workshop_supply_owner.field_e0 (layout_offset): value=0xE0; Raw diagnostic workshop supply owner layout read; do not treat as an executable RVA, Address Library candidate, or auto-update target.
- workshop_supply_owner.field_e8 (layout_offset): value=0xE8; Raw diagnostic workshop supply owner layout read; do not treat as an executable RVA, Address Library candidate, or auto-update target.
- workshop_supply_owner.field_f8 (layout_offset): value=0xF8; Raw diagnostic workshop supply owner layout read; do not treat as an executable RVA, Address Library candidate, or auto-update target.
- workshop_supply_owner.field_2f8 (layout_offset): value=0x2F8; Raw diagnostic workshop supply owner layout read; do not treat as an executable RVA, Address Library candidate, or auto-update target.

## Unresolved Items Checklist
- [ ] workshop_material.resource_status_missing_resources: Semantic constant was retained without executable-address proof for this runtime; verify separately before changing it.
- [ ] workshop_supply_owner.field_e0: Diagnostic layout offset was retained without executable-address proof for this runtime; verify separately before changing it.
- [ ] workshop_supply_owner.field_e8: Diagnostic layout offset was retained without executable-address proof for this runtime; verify separately before changing it.
- [ ] workshop_supply_owner.field_f8: Diagnostic layout offset was retained without executable-address proof for this runtime; verify separately before changing it.
- [ ] workshop_supply_owner.field_2f8: Diagnostic layout offset was retained without executable-address proof for this runtime; verify separately before changing it.
- [ ] Manual release gate: complete F4SE load and in-game smoke testing.
