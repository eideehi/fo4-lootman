# LootMan Native Hook Address Review Bundle

Binary version: Fallout4 1.11.191
Manifest target runtime: Fallout4 1.11.191

## Bundle Files
- Manifest copy: tools/native-hooks/reports/fallout4-1.11.191/manifest.json
- Candidate RVAs: tools/native-hooks/reports/fallout4-1.11.191/candidate-rvas.json
- Current hook source slice: tools/native-hooks/reports/fallout4-1.11.191/papyrus_lootman_hooks.slice.cpp

## Candidate RVAs
- encounter_zone.load_change_cell_before_zone_reset (call_site_rva, unproven): encounter-zone.reset-suppression.cell-before-reset=0x4D23C4
- encounter_zone.reset_elapsed_from_detach_time (function_rva, unproven): value=0x4D2E20
- workshop_shared_container.workshop_caravan_keyword_global (global_rva, unproven): value=0x30EC9B8
- workshop_material.current_workshop_handle_global (global_rva, unproven): value=0x30EC598
- workshop_shared_container.populate_linked_workshop_container (call_site_rva, unproven): workshop-shared-container.populate-linked.primary=0x391F78, workshop-shared-container.populate-linked.workbench=0xB28B76, workshop-shared-container.populate-linked.menu=0x10890F6
- workshop_material.rebuild_workshop_supply (call_site_rva, unproven): workshop-material.rebuild-supply.source-a1=0xA653F6, workshop-material.rebuild-supply.source-a2=0xA5F109, workshop-material.rebuild-supply.source-a3=0xA6052C, workshop-material.rebuild-supply.source-a4=0xAEFD89
- workshop_material.component_count_helper (call_site_rva, unproven): workshop-material.component-count.papyrus=0x59BC2A, workshop-material.component-count.workbench-ui=0x117501B
- workshop_material.direct_component_count (call_site_rva, proven): workshop-material.direct-component-count.source-e1=0x3BC3ED, workshop-material.direct-component-count.source-e2=0x39F27F, workshop-material.direct-component-count.source-e3=0xB3308B, workshop-material.direct-component-count.source-e4=0xB37A38, workshop-material.direct-component-count.source-e5=0xB2D34E
- workshop_material.resource_status (call_site_rva, unproven): workshop-material.resource-status.source-f1=0xB2F2C0, workshop-material.resource-status.source-f2=0xB2D266
- workshop_menu.select (call_site_rva, unproven): workshop-menu.select.source-a1=0xB2C8AA, workshop-menu.select.source-a2=0xB2CB67
- workshop_menu.availability (call_site_rva, unproven): workshop-menu.availability.source-91=0xB2C86E, workshop-menu.availability.source-92=0xB2C8D7, workshop-menu.availability.source-93=0xB2CB2E, workshop-menu.availability.source-94=0xB2CB94, workshop-menu.availability.source-95=0xB2EBE4
- workshop_menu.check_and_set_placement (call_site_rva, unproven): workshop-menu.check-placement.source-a5=0xB2B307, workshop-menu.check-placement.source-a6=0xB2C8F2, workshop-menu.check-placement.source-a7=0xB2CBAF, workshop-menu.check-placement.source-a8=0xB2E88E
- workshop_menu.start_placement (call_site_rva, unproven): workshop-menu.start-placement.source-a3=0xB2C9EA, workshop-menu.start-placement.source-a4=0xB2CCA5
- workshop_material.build_resource_check (call_site_rva, unproven): workshop-material.build-resource-check.placement=0x392514, workshop-material.build-resource-check.confirm=0x398E06
- workshop_material.consume_component (call_site_rva, unproven): workshop-material.consume-component.source-f3=0x398FF6, workshop-material.consume-component.source-f4=0x3B7D2A
- workshop_menu.selected_menu_node_function (function_rva, unproven): value=0x389A80
- workshop_menu.selected_row_global (global_rva, unproven): value=0x30EBE18
- workshop_material.resource_status_missing_resources (constant, manual): value=2
- workshop_material.remove_components (call_site_rva, unproven): workshop-material.remove-components.source-f1=0x114EB19, workshop-material.remove-components.source-f2=0x114E543
- workshop_material.object_count_papyrus (call_site_rva, proven): workshop-material.object-count.papyrus=0x5DD484
- workshop_material.current_workshop_object_count (call_site_rva, proven): workshop-material.object-count.current-workshop=0x59D378
- workshop_supply_owner.field_e0 (layout_offset, manual): value=0xE0
- workshop_supply_owner.field_e8 (layout_offset, manual): value=0xE8
- workshop_supply_owner.field_f8 (layout_offset, manual): value=0xF8
- workshop_supply_owner.field_2f8 (layout_offset, manual): value=0x2F8

## Instruction Windows
- encounter_zone.load_change_cell_before_zone_reset: 0x4D23C4
- workshop_shared_container.populate_linked_workshop_container: 0x391F78, 0xB28B76, 0x10890F6
- workshop_material.rebuild_workshop_supply: 0xA653F6, 0xA5F109, 0xA6052C, 0xAEFD89
- workshop_material.component_count_helper: 0x59BC2A, 0x117501B
- workshop_material.direct_component_count: 0x3BC3ED, 0x39F27F, 0xB3308B, 0xB37A38, 0xB2D34E
- workshop_material.resource_status: 0xB2F2C0, 0xB2D266
- workshop_menu.select: 0xB2C8AA, 0xB2CB67
- workshop_menu.availability: 0xB2C86E, 0xB2C8D7, 0xB2CB2E, 0xB2CB94, 0xB2EBE4
- workshop_menu.check_and_set_placement: 0xB2B307, 0xB2C8F2, 0xB2CBAF, 0xB2E88E
- workshop_menu.start_placement: 0xB2C9EA, 0xB2CCA5
- workshop_material.build_resource_check: 0x392514, 0x398E06
- workshop_material.consume_component: 0x398FF6, 0x3B7D2A
- workshop_material.remove_components: 0x114EB19, 0x114E543
- workshop_material.object_count_papyrus: 0x5DD484
- workshop_material.current_workshop_object_count: 0x59D378

## Proof Readiness
### needs_instruction_window_refresh
- workshop_shared_container.populate_linked_workshop_container: target=0x140389E10; selectedRefs=3/3; directCalls=1/3; extras=0; Refresh instruction-window evidence for 0x140391F78, 0x1410890F6.
- workshop_material.rebuild_workshop_supply: target=0x140B29690; selectedRefs=4/4; directCalls=0/4; extras=0; Refresh instruction-window evidence for 0x140A5F109, 0x140A6052C, 0x140A653F6, 0x140AEFD89.
- workshop_material.component_count_helper: target=0x140507660; selectedRefs=2/2; directCalls=1/2; extras=0; Refresh instruction-window evidence for 0x14117501B.
- workshop_menu.select: target=0x140396DB0; selectedRefs=2/2; directCalls=0/2; extras=0; Refresh instruction-window evidence for 0x140B2C8AA, 0x140B2CB67.
### needs_target_allrefs_report
- workshop_material.resource_status: target=0x140B32FB0; selectedRefs=n/a; directCalls=0/2; extras=0; Generate a target allrefs report for 0x140B32FB0.
- workshop_menu.check_and_set_placement: target=0x140B2EB50; selectedRefs=n/a; directCalls=3/4; extras=0; Generate a target allrefs report for 0x140B2EB50.
- workshop_menu.start_placement: target=0x140B30140; selectedRefs=n/a; directCalls=2/2; extras=0; Generate a target allrefs report for 0x140B30140.
### needs_exclusion_triage
- workshop_menu.availability: target=0x1403997A0; selectedRefs=5/5; directCalls=4/5; extras=14; Triage 14 extra same-target references before adding exclusions.
### needs_rediscovery
- encounter_zone.load_change_cell_before_zone_reset: target=none; selectedRefs=n/a; directCalls=0/1; extras=0; Refresh discovery reports around the selected manifest sites.
- workshop_material.build_resource_check: target=none; selectedRefs=n/a; directCalls=0/2; extras=0; Refresh discovery reports around the selected manifest sites.
- workshop_material.consume_component: target=none; selectedRefs=n/a; directCalls=0/2; extras=0; Refresh discovery reports around the selected manifest sites.
- workshop_material.remove_components: target=none; selectedRefs=n/a; directCalls=0/2; extras=0; Refresh discovery reports around the selected manifest sites.
### already_proven
- workshop_material.direct_component_count: target=0x140507A00; selectedRefs=n/a; directCalls=5/5; extras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.object_count_papyrus: target=0x14059D360; selectedRefs=n/a; directCalls=1/1; extras=0; No proof refresh needed; resolver proof metadata is already present.
- workshop_material.current_workshop_object_count: target=0x14037DE60; selectedRefs=n/a; directCalls=1/1; extras=0; No proof refresh needed; resolver proof metadata is already present.
### not_applicable
- encounter_zone.reset_elapsed_from_detach_time: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_shared_container.workshop_caravan_keyword_global: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_material.current_workshop_handle_global: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_menu.selected_menu_node_function: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_menu.selected_row_global: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_material.resource_status_missing_resources: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_supply_owner.field_e0: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_supply_owner.field_e8: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_supply_owner.field_f8: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.
- workshop_supply_owner.field_2f8: target=none; selectedRefs=n/a; directCalls=n/a; extras=0; No call-site proof refresh needed.

## Referenced Ghidra Reports
- tools/ghidra/reports/fo4-can-produce-workshop.txt
- tools/ghidra/reports/fo4-canproduce-callers-detail.txt
- tools/ghidra/reports/fo4-canproduce-deps.txt
- tools/ghidra/reports/fo4-cell-detach-reset-functions.txt
- tools/ghidra/reports/fo4-component-helper-callers.txt
- tools/ghidra/reports/fo4-current-workshop-global-refs.txt
- tools/ghidra/reports/fo4-direct-component-count-allrefs.txt
- tools/ghidra/reports/fo4-direct-component-count-callsites-window.txt
- tools/ghidra/reports/fo4-encounter-zone-detach-callers.txt
- tools/ghidra/reports/fo4-get-component-count.txt
- tools/ghidra/reports/fo4-get-workshop-object-count.txt
- tools/ghidra/reports/fo4-placement-resource-status-functions.txt
- tools/ghidra/reports/fo4-placement-secondary-globals.txt
- tools/ghidra/reports/fo4-placement-set-callers.txt
- tools/ghidra/reports/fo4-placement-set-windows.txt
- tools/ghidra/reports/fo4-remove-component-consume-core.txt
- tools/ghidra/reports/fo4-remove-component-core.txt
- tools/ghidra/reports/fo4-remove-component-functions.txt
- tools/ghidra/reports/fo4-removeitem-wrapper-callers.txt
- tools/ghidra/reports/fo4-selected-menu-helper-functions.txt
- tools/ghidra/reports/fo4-workbench-linked-container-functions.txt
- tools/ghidra/reports/fo4-workbench-shared-container-callers.txt
- tools/ghidra/reports/fo4-workbench-supply-functions.txt
- tools/ghidra/reports/fo4-workbench-ui-callbacks.txt
- tools/ghidra/reports/fo4-workshop-count-helper.txt
- tools/ghidra/reports/fo4-workshop-object-count-functions.txt
- tools/ghidra/reports/fo4-workshopmenu-b2e900-b35320-b35440.txt
- tools/ghidra/reports/fo4-workshopmenu-placement-creation-paths.txt
- tools/ghidra/reports/fo4-workshopmenu-placement-writes.txt

## Unresolved Items Checklist
- [ ] encounter_zone.load_change_cell_before_zone_reset: Discovery strategy is unproven: Resolve the LoadChange cell reset path and require one direct CALL rel32 to the check-cell-before-reset target.
- [ ] encounter_zone.load_change_cell_before_zone_reset: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] encounter_zone.reset_elapsed_from_detach_time: Discovery strategy is unproven: Resolve the detach-time elapsed helper from the encounter-zone reset analysis before updating this RVA.
- [ ] workshop_shared_container.workshop_caravan_keyword_global: Discovery strategy is unproven: Re-derive from shared workshop container callers and verify the referenced global still resolves to the caravan keyword.
- [ ] workshop_material.current_workshop_handle_global: Discovery strategy is unproven: Resolve the current-workshop handle global from Ghidra references and require the current workshop context probes to agree.
- [ ] workshop_shared_container.populate_linked_workshop_container: Discovery strategy is unproven: Find all callers of the linked workshop container population helper and require exactly three direct CALL rel32 sites.
- [ ] workshop_shared_container.populate_linked_workshop_container: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_material.rebuild_workshop_supply: Discovery strategy is unproven: Resolve workbench supply rebuild callers and require four direct CALL rel32 sites to the same rebuild helper.
- [ ] workshop_material.rebuild_workshop_supply: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_material.component_count_helper: Discovery strategy is unproven: Resolve Papyrus and Workbench UI calls to the component count helper and require both call sites to share the same original target.
- [ ] workshop_material.component_count_helper: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_material.resource_status: Discovery strategy is unproven: Resolve resource status helper callers in the workshop menu placement path and require two direct CALL rel32 sites.
- [ ] workshop_material.resource_status: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_menu.select: Discovery strategy is unproven: Resolve SelectWorkshopMenuNode call sites from selected menu helper analysis and require two direct CALL rel32 sites.
- [ ] workshop_menu.select: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_menu.availability: Discovery strategy is unproven: Resolve WorkshopMenuAvailability callers and require five direct CALL rel32 sites to the same helper.
- [ ] workshop_menu.availability: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_menu.check_and_set_placement: Discovery strategy is unproven: Resolve CheckAndSetItemForPlacement callers and require four direct CALL rel32 sites to the same placement helper.
- [ ] workshop_menu.check_and_set_placement: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_menu.start_placement: Discovery strategy is unproven: Resolve StartWorkshopPlacement callers and require two direct CALL rel32 sites to the same helper.
- [ ] workshop_menu.start_placement: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_material.build_resource_check: Discovery strategy is unproven: Resolve build resource checks from CanProduceWorkshop analysis and require two direct CALL rel32 sites.
- [ ] workshop_material.build_resource_check: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_material.consume_component: Discovery strategy is unproven: Resolve workshop component consumption calls and require two direct CALL rel32 sites to the same consume helper.
- [ ] workshop_material.consume_component: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_menu.selected_menu_node_function: Discovery strategy is unproven: Resolve the selected workshop menu node helper from selected menu helper analysis.
- [ ] workshop_menu.selected_row_global: Discovery strategy is unproven: Resolve the selected row global from selected workshop menu helper references.
- [ ] workshop_material.resource_status_missing_resources: Discovery strategy is manual: This is a semantic status value, not an executable RVA; update only if the resource status enum is re-proven.
- [ ] workshop_material.remove_components: Discovery strategy is unproven: Resolve RemoveComponents wrappers and require two direct CALL rel32 sites to the same remove helper.
- [ ] workshop_material.remove_components: Verify candidate count, CALL rel32 shape, and original target grouping before updating manifest RVAs.
- [ ] workshop_supply_owner.field_e0: Discovery strategy is manual: Raw diagnostic workshop supply owner layout read; do not treat as an executable RVA, Address Library candidate, or auto-update target.
- [ ] workshop_supply_owner.field_e0: Layout offset is not an executable RVA; verify object layout separately before changing it.
- [ ] workshop_supply_owner.field_e8: Discovery strategy is manual: Raw diagnostic workshop supply owner layout read; do not treat as an executable RVA, Address Library candidate, or auto-update target.
- [ ] workshop_supply_owner.field_e8: Layout offset is not an executable RVA; verify object layout separately before changing it.
- [ ] workshop_supply_owner.field_f8: Discovery strategy is manual: Raw diagnostic workshop supply owner layout read; do not treat as an executable RVA, Address Library candidate, or auto-update target.
- [ ] workshop_supply_owner.field_f8: Layout offset is not an executable RVA; verify object layout separately before changing it.
- [ ] workshop_supply_owner.field_2f8: Discovery strategy is manual: Raw diagnostic workshop supply owner layout read; do not treat as an executable RVA, Address Library candidate, or auto-update target.
- [ ] workshop_supply_owner.field_2f8: Layout offset is not an executable RVA; verify object layout separately before changing it.
