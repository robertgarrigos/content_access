<?php

/**
 * @file Content access administration UI.
 */

/**
 * Per node settings page.
 */
function content_access_page($form, &$form_state, $node) {
  backdrop_set_title(t('Access control for @title', array('@title' => $node->title)));

  foreach (_content_access_get_operations() as $op => $label) {
    $defaults[$op] = content_access_per_node_setting($op, $node);
  }

  // Get roles form.
  content_access_role_based_form($form, $defaults, $node->type);

  // Add an after_build handler that disables checkboxes, which are enforced by permissions.
  $form['per_role']['#after_build'] = array('content_access_force_permissions');

  // ACL form.
  if (module_exists('acl')) {
    // This is disabled when there is no node passed.
    $form['acl'] = array(
      '#type' => 'fieldset',
      '#title' => t('User access control lists'),
      '#description' => t('These settings allow you to grant access to specific users.'),
      '#collapsible' => TRUE,
      '#collapsed' => FALSE,
      '#tree' => TRUE,
    );

    foreach (array('view', 'update', 'delete') as $op) {
      $acl_id = content_access_get_acl_id($node, $op);
      acl_node_add_acl($node->nid, $acl_id, (int) ($op == 'view'), (int) ($op == 'update'), (int) ($op == 'delete'), content_access_get_settings('priority', $node->type));

      $form['acl'][$op] = acl_edit_form($form_state, $acl_id, t('Grant !op access', array('!op' => $op)));

      $data = acl_edit_form_get_user_list($form['acl'][$op], TRUE);
      if (!isset($_POST['acl_' . $acl_id]) && !$data) {
        $form['acl'][$op]['#collapsed'] = TRUE;
      }
    }
  }

  $form_state['node'] = $node;

  $form['reset'] = array(
    '#type' => 'submit',
    '#value' => t('Reset to defaults'),
    '#weight' => 10,
    '#submit' => array('content_access_page_reset'),
    '#access' => !empty(content_access_get_per_node_settings($node)),
  );

  $form['actions']['#type'] = 'actions';
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save configuration'),
  );

  // @todo not true anymore?
  // http://drupal.org/update/modules/6/7#hook_node_access_records
  if (!$node->status) {
    backdrop_set_message(t("Warning: Your content is not published, so these settings are not taken into account as long as the content remains unpublished."), 'error');
  }

  return $form;
}

/**
 * Submit callback for content_access_page().
 */
function content_access_page_submit($form, &$form_state) {
  $settings = array();
  $node = $form_state['node'];

  foreach (_content_access_get_operations() as $op => $label) {
    // Set the settings so that further calls will return this settings.
    $settings[$op] = array_keys(array_filter($form_state['values'][$op]));
  }

  // Save per-node settings.
  content_access_save_per_node_settings($node, $settings);

  if (module_exists('acl')) {
    foreach (array('view', 'update', 'delete') as $op) {
      acl_save_form($form_state['values']['acl'][$op]);
      module_invoke_all('user_acl', $settings);
    }
  }

  // Apply new settings.
  node_access_acquire_grants($node);
  cache_clear_all();
  module_invoke_all('per_node', $settings, $node);
  backdrop_set_message(t('Your changes have been saved. You may have to !rebuild for your changes to take effect.', array('!rebuild' => l(t('rebuild permissions'), 'admin/reports/status/rebuild'))));
}

/**
 * Submit callback for reset on content_access_page().
 */
function content_access_page_reset($form, &$form_state) {
  content_access_delete_per_node_settings($form_state['node']);
  node_access_acquire_grants($form_state['node']);

  backdrop_set_message(t('The permissions have been reset to the content type defaults.'));
}

/**
 * Per content type settings form.
 */
function content_access_admin_settings($form, &$form_state, $content_type) {
  $type = $content_type->type;

  $form_state['type'] = $type;

  // Add role based per content type settings.
  $defaults = array();
  foreach (_content_access_get_operations() as $op => $label) {
    $defaults[$op] = content_access_get_settings($op, $type);
  }
  content_access_role_based_form($form, $defaults, $type);

  // Per node:
  $form['node'] = array(
    '#type' => 'fieldset',
    '#title' => t('Per content node access control settings'),
    '#collapsible' => TRUE,
    '#description' => t('Optionally you can enable per content node access control settings. If enabled, a new tab for the content access settings appears when viewing content. You have to configure permission to access these settings at the !permissions page.', array('!permissions' => l(t('permissions'), 'admin/config/people/permissions'))),
  );
  $form['node']['per_node'] = array(
    '#type' => 'checkbox',
    '#title' => t('Enable per content node access control settings'),
    '#default_value' => content_access_get_settings('per_node', $type),
  );

  $form['advanced'] = array(
    '#type' => 'fieldset',
    '#title' => t('Advanced'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );
  $form['advanced']['priority'] = array(
    '#type' => 'weight',
    '#title' => t('Give content node grants priority'),
    '#default_value' => content_access_get_settings('priority', $type),
    '#description' => t('If you are only using this access control module, you can safely ignore this. If you are using multiple access control modules you can adjust the priority of this module.'),
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Submit'),
    '#weight' => 10,
  );

  return $form;
}

/**
 * Submit handler for per content type settings form.
 */
function content_access_admin_settings_submit($form, &$form_state) {
  $roles_permissions = array();
  foreach (user_roles(FALSE, NULL, TRUE) as $machine_name => $role_object) {
    foreach ($role_object->permissions as $key => $permission) {
      $roles_permissions[$machine_name][$permission] = TRUE;
    }
  }
  $permissions = user_permission_get_modules();
  $type = $form_state['type'];

  // Remove disabled modules permissions, so they can't raise exception
  // in content_access_save_permissions()
  foreach ($roles_permissions as $rid => $role_permissions) {
    foreach ($role_permissions as $permission => $value) {
      if (!array_key_exists($permission, $permissions)) {
        unset($roles_permissions[$rid][$permission]);
      }
    }
  }

  foreach (array('update', 'update_own', 'delete', 'delete_own') as $op) {
    foreach ($form_state['values'][$op] as $rid => $value) {
      $permission = content_access_get_permission_by_op($op, $form_state['type']);
      if ($value) {
        $roles_permissions[$rid][$permission] = TRUE;
      }
      else {
        $roles_permissions[$rid][$permission] = FALSE;
      }
    }
    // Don't save the setting, so its default value (get permission) is applied
    // always.
    unset($form_state['values'][$op]);
  }
  content_access_save_permissions($roles_permissions);

  // Update content access settings.
  $settings = content_access_get_settings('all', $type);
  foreach (content_access_available_settings() as $setting) {
    if (isset($form_state['values'][$setting])) {
      $settings[$setting] = is_array($form_state['values'][$setting]) ? array_keys(array_filter($form_state['values'][$setting])) : $form_state['values'][$setting];
    }
  }
  content_access_set_settings($settings, $type);

  // Mass update the nodes, but only if necessary.
  if (content_access_get_settings('per_node', $type) ||
      content_access_get_settings('view', $type) != $form['per_role']['view']['#default_value'] ||
      content_access_get_settings('view_own', $type) != $form['per_role']['view_own']['#default_value'] ||
      content_access_get_settings('priority', $type) != $form['advanced']['priority']['#default_value'] ||
      content_access_get_settings('per_node', $type) != $form['node']['per_node']['#default_value']
     ) {

    // If per node has been disabled and we use the ACL integration, we have to remove possible ACLs now.
    if (!content_access_get_settings('per_node', $type) && $form['node']['per_node']['#default_value'] && module_exists('acl')) {
      _content_access_remove_acls($type);
    }

    if (content_access_mass_update(array($type))) {
      backdrop_set_message(t('Permissions have been successfully rebuilt for the content type @types.', array('@types' => node_type_get_name($type))));
    }
  }

  backdrop_set_message(t('Your changes have been saved. You may have to !rebuild for your changes to take effect.', array('!rebuild' => l(t('rebuild permissions'), 'admin/reports/status/rebuild'))));
}

/**
 * Mass updates node access records for nodes of the given types.
 * @param $types
 *   An array of content type names.
 * @return bool
 *   Whether the operation has been processed successfully (TRUE) or postponed (FALSE).
 */
function content_access_mass_update($types) {
  $q = db_select('node', 'n')
    ->fields('n', array('nid'))
    ->condition('n.type', $types, 'IN');

  $count = $q->countQuery()->execute()->fetchField();

  node_access_needs_rebuild(TRUE);

  // If there not too much nodes affected, try to do it.
  if ($count <= CONTENT_ACCESS_MASS_UPDATE_THRESHOLD) {
    $records = $q->execute();
    foreach ($records as $node) {
      node_access_acquire_grants(node_load($node->nid));
    }

    cache_clear_all();
    node_access_needs_rebuild(FALSE);
    return TRUE;
  }
  return FALSE;
}

/**
 * Saves the given permissions by role to the database.
 */
function content_access_save_permissions($roles_permissions) {
  foreach ($roles_permissions as $rid => $permissions) {
    user_role_change_permissions($rid, $permissions);
  }
}

/**
 * Builds the role based permission form for the given defaults.
 *
 * @param $defaults
 *   Array of defaults for all operations.
 */
function content_access_role_based_form(&$form, $defaults = array(), $type = NULL) {
  $form['per_role'] = array(
    '#type' => 'fieldset',
    '#title' => t('Role based access control settings'),
    '#collapsible' => TRUE,
    '#description' => t('Note that users need at least the %access_content permission to be able to deal in any way with content.', array('%access_content' => t('access content'))) .
    ' ' . t('Furthermore note that content which is not @published is treated in a different way by Backdrop: it can be viewed only by its author or users with the %administer_nodes permission.', array('@published' => t('published'), '%administer_nodes' => t('administer nodes'))),
  );

  $operations = _content_access_get_operations($type);
  $roles = array_map('filter_xss_admin', user_roles());
  foreach ($operations as $op => $label) {
    // Make sure defaults are set properly.
    $defaults += array($op => array());

    $form['per_role'][$op] = array(
      '#type' => 'checkboxes',
      '#prefix' => '<div class="content_access-div">',
      '#suffix' => '</div>',
      '#options' => $roles,
      '#title' => $label,
      '#default_value' => $defaults[$op],
      '#process' => array('form_process_checkboxes', 'content_access_disable_checkboxes'),
    );
  }

  $form['per_role']['clearer'] = array(
    '#value' => '<br clear="all" />',
  );

  backdrop_add_css(backdrop_get_path('module', 'content_access') . '/content_access.css');

  return $form;
}

/**
 * Formapi #after_build callback, that disables checkboxes for roles without access to content.
 */
function content_access_force_permissions($element, &$form_state) {
  foreach (array('update', 'update_own', 'delete', 'delete_own') as $op) {
    foreach (content_access_get_settings($op, $form_state['node']->type) as $rid) {
      $element[$op][$rid]['#disabled'] = TRUE;
      $element[$op][$rid]['#attributes']['disabled'] = 'disabled';
      $element[$op][$rid]['#value'] = TRUE;
      $element[$op][$rid]['#checked'] = TRUE;
      $element[$op][$rid]['#prefix'] = '<span' . backdrop_attributes(array('title' => t("Permission is granted due to the content type's access control settings."))) . '>';
      $element[$op][$rid]['#suffix'] = "</span>";
    }
  }
  return $element;
}

/**
 * Submit callback for the user permissions form.
 * Trigger changes to node permissions to rebuild our grants.
 */
function content_access_user_admin_perm_submit($form, $form_state) {
  // Check for each content type, which has per node access activated
  // whether permissions have been changed.
  $types = array();
  foreach (array_filter(content_access_get_settings('per_node')) as $type => $value) {
    foreach (_content_access_get_node_permissions($type) as $perm) {
      foreach (user_roles() as $rid => $role) {
        if (isset($form_state['values'][$rid]) && in_array($perm, $form['checkboxes'][$rid]['#default_value']) != in_array($perm, $form_state['values'][$rid])) {
          // Permission changed!
          $types[$type] = node_get_types('name', $type);
          continue 2;
        }
      }
    }
  }
  if ($types && content_access_mass_update(array_keys($types))) {
    backdrop_set_message(format_plural(count($types),
      'Permissions have been successfully rebuilt for the content type @types.',
      'Permissions have been successfully rebuilt for the content types @types.',
      array('@types' => implode(', ', $types))
    ));
  }
}

/**
 * Global settings form for Content Access module.
 *
 * @param $form
 * @param $form_state
 *
 * @return mixed
 */
function content_access_global_settings($form, &$form_state) {
  $silent_rebuild = config_get('content_access.settings', 'silent_rebuild');
  if (!$silent_rebuild) {
    backdrop_set_message("Please be aware of disabling background rebuild option or running the website without regular cron jobs makes the contents' permissions unreliable. In this case, site administrator has to run the rebuild process manually when a user's role is being changed.", 'warning');
  }

  $form['#config'] = 'content_access.settings';
  $form['silent_rebuild'] = array(
    '#type' => 'checkbox',
    '#title' => t('Allow background permission rebuild'),
    '#description' => t('Rebuild permissions in the background during cron runs. This is recommended on sites with more than @threshold nodes/pages. Permission rebuilding may take longer, but it will prevent 403 errors for some users. (This requires a <a href="@cron-url" target="_blank">properly configured cron job</a>.)<br /><strong>Note:</strong> If you check this box, role changes will <em>not</em> have immediate effect on role based content access, but will only take place after cron has been run.',
      array(
        '@threshold' => number_format(CONTENT_ACCESS_MASS_UPDATE_THRESHOLD),
        '@cron-url' => 'https://docs.backdropcms.org/documentation/setting-up-cron',
      )
    ),
    '#default_value' => $silent_rebuild,
  );

  if ($data = content_access_get_rebuild_data()) {
    $form['rebuild_permission'] = array(
      '#markup' => t('You can run the !link from here. !count record(s) are waited for to be processed.',
        array(
          '!link' => l('rebuild permission process', 'admin/config/system/content_access/rebuild'),
          '!count' => count($data),
        )
      ),
    );
  }

  return system_settings_form($form);
}

/**
 *
 * Menu callback: confirm rebuilding of permissions.
 * @param $form
 * @param $form_state
 *
 * @return void
 */
function content_access_rebuild_confirm($form, &$form_state) {
  return confirm_form(
    array(),
    t("Are you sure you want to rebuild the pending permissions on site's contents?"),
    'admin/config/system/content_access',
    t('This action will rebuild pending permissions on site content, and may be a lengthy process. This action cannot be undone.'),
    t('Rebuild permissions'),
    t('Cancel')
  );
}

/**
 * Handler for wipe confirmation
 *
 * @see node_configure_rebuild_confirm()
 */
function content_access_rebuild_confirm_submit($form, &$form_state) {
  content_access_rebuild_permission(TRUE);
  $form_state['redirect'] = 'admin/config/system/content_access';
}
