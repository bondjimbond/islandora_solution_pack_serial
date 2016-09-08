<?php
/**
 * @file
 * Create variable intermediate field forms
 */

/**
 * Form to add a single intermediate object.
 *
 * @param array $form
 *   An array representing a form in Drupal.
 * @param array $form_state
 *   An array containing the Drupal form state.
 * @param AbstractObject $object
 *   An AbstractObject representing a Fedora object.
 *
 * @return array
 *   A renderable array containing the Drupal form.
 */
function islandora_serial_object_add_intermediate_form(array $form, array &$form_state, AbstractObject $parent_object) {

  $form_state['parent_object'] = $parent_object;
  $tuque = islandora_get_tuque_connection();
  $query = <<<'EOQ'
SELECT ?subject ?content
FROM <#ri>
WHERE {
  ?subject <fedora-model:hasModel> ?content .
  <info:fedora/!pid> <fedora-rels-ext:isMemberOf>+ ?subject .
FILTER (sameTerm(?content, <info:fedora/islandora:rootSerialCModel>) || sameTerm(?content, <info:fedora/islandora:intermediateCModel>))
}
EOQ;
  $formatted_query = format_string($query, array(
    '!pid' => $parent_object->id,
  ));
  $results = $tuque->repository->ri->sparqlQuery($formatted_query);
  $level_counter = 1;
  $root_serial_pid = '';
  foreach ($results as $result) {
    $level_counter++;
    if ($result['content']['value'] == 'islandora:rootSerialCModel') {
      $root_serial_pid = str_replace('info:fedora/', '', $result['subject']['value']);
    }
  }
  if (in_array('islandora:rootSerialCModel', $parent_object->models)) {
    $root_serial_pid = $parent_object->id;
    $level_counter = 1;
  }
  $root_serial_object = islandora_object_load($root_serial_pid);
  $type = '';
  $caption = '';
  $default_values = json_decode($root_serial_object['SERIAL-LEVELS']->content, TRUE);
  $type = $default_values[$level_counter]['type'];
  $caption = $default_values[$level_counter]['caption'];

  $form['type'] = array(
    '#title' => t('Type'),
    '#type' => 'textfield',
    '#size' => 30,
    '#default_value' => $type,
    '#disabled' => TRUE,
  );
  $form['caption'] = array(
    '#title' => t('Caption'),
    '#type' => 'textfield',
    '#size' => 30,
    '#default_value' => $caption,
  );

  $form['number'] = array(
    '#title' => t('Number'),
    '#type' => 'textfield',
    '#size' => 30,
    '#default_value' => '',
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Add')
  );

  return $form;
}

/**
 * Form to add single intermediate object submit handler.
 *
 * @param array $form
 *   An array representing a form in Drupal.
 * @param array $form_state
 *   An array containing the Drupal form state.
 */
function islandora_serial_object_add_intermediate_form_submit($form, &$form_state) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  module_load_include('inc', 'islandora_serial_object', 'includes/utilities');

  $parent_pid = $form_state['parent_object'];
  $parent_object = islandora_object_load($parent_pid);
  $sequence_number = islandora_serial_object_get_max_intermediate_sequence_number($parent_object->id) + 1;
  $type = $form_state['values']['type'];
  $caption = $form_state['values']['caption'];
  $number = $form_state['values']['number'];

  //drupal_set_message('added sequence number ' . $sequence_number . ' for ' . $parent_pid . ' type=' . $type . ' caption=' . $caption . ' number=' . $number);

  $object_pid = islandora_get_namespace($parent_pid);

  $tuque = islandora_get_tuque_connection();
  $object = $tuque->repository->constructObject($object_pid);
  $object_is_new = TRUE;

  $mods_doc = new DOMDocument();
  $update_type = FALSE;

  $mods_datastream = $object->constructDatastream('MODS', 'M');
  $mods_datastream_is_new = TRUE;
  $special_type = htmlspecialchars($type);
  $mods_string = <<<EOXML
<?xml version="1.0" encoding="UTF-8"?>
<mods
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xmlns="http://www.loc.gov/mods/v3"
  xsi:schemaLocation="http://www.loc.gov/mods/v3 http://www.loc.gov/standards/mods/v3/mods-3-4.xsd"
    version="3.4">
  <relatedItem type="host">
    <part>
      <detail type="{$special_type}">
         <caption/>
         <number/>
      </detail>
     </part>
  </relatedItem>
</mods>
EOXML;

  $mods_doc->loadXML($mods_string);
  $mods_xpath = new DOMXPath($mods_doc);
  $mods_xpath->registerNamespace('mods', 'http://www.loc.gov/mods/v3');

  $caption_xpath = islandora_serial_object_replace_xpath(ISLANDORA_SERIAL_OBJECT_MODS_CAPTION_XPATH, htmlspecialchars($type));
  $mods_caption_xpath = $mods_xpath->query($caption_xpath);
  if ($mods_caption_xpath->length) {
    $mods_caption = $mods_caption_xpath->item(0);
    $mods_caption->nodeValue = htmlspecialchars($caption);
  }
  $number_xpath = islandora_serial_object_replace_xpath(ISLANDORA_SERIAL_OBJECT_MODS_NUMBER_XPATH, htmlspecialchars($type));
  $mods_number_xpath = $mods_xpath->query($number_xpath);
  if ($mods_number_xpath->length) {
    $mods_number = $mods_number_xpath->item(0);
    $mods_number->nodeValue = htmlspecialchars($number);
  }
  $mods_datastream->content = $mods_doc->saveXML();

  // Only want to update the label if the object does not have one.
  if (empty($object->label)) {
    module_load_include('inc', 'islandora_serial_object', 'includes/utilities');
    $label = array(
      'caption' => $caption,
      'number' => $number,
    );
    $label = islandora_serial_object_filter_mods_data($label);
    $object->label = implode(' ', $label);
  }
  $object->relationships->autoCommit = FALSE;
  $object->relationships->remove(
    FEDORA_RELS_EXT_URI,
    'isMemberOf'
  );
  $object->relationships->add(
    FEDORA_RELS_EXT_URI,
    'isMemberOf',
    $parent_pid,
    RELS_TYPE_URI
  );
  $object->relationships->remove(
    ISLANDORA_RELS_EXT_URI,
    ISLANDORA_SERIAL_OBJECT_SEQUENCE_PREDICATE
  );
  $object->relationships->add(
    ISLANDORA_RELS_EXT_URI,
    ISLANDORA_SERIAL_OBJECT_SEQUENCE_PREDICATE,
    $sequence_number,
    RELS_TYPE_INT
  );
  $object->relationships->commitRelationships();

  // Ingest if necessary.
  if ($mods_datastream_is_new) {
    $mods_datastream->label = 'MODS Metadata';
    $mods_datastream->mimetype = 'text/xml';
    $object->ingestDatastream($mods_datastream);
  }
  if ($object_is_new) {
    $object->models = 'islandora:intermediateCModel';
    islandora_add_object($object);
  }

}

/**
 * Retrieves the highest sequence number of an intermediate child object.
 *
 * @param string $parent_id
 *   The pid of the Fedora object that is the parent.
 *
 * @return int
 *   0 if the object has no child, the maximum otherwise.
 */
function islandora_serial_object_get_max_intermediate_sequence_number($parent_id) {
  $tuque = islandora_get_tuque_connection();
  $sequence_predicate = ISLANDORA_SERIAL_OBJECT_SEQUENCE_PREDICATE;
  $intermediate_parent_predicate = 'isMemberOf';
  $islandora_namespace = ISLANDORA_RELS_EXT_URI;
  $fedora_namespace = FEDORA_RELS_EXT_URI;
  $sequence_query = <<<'EOQ'
SELECT ?sequence_number
FROM <#ri>
WHERE {
    ?child <!parent_predicate> <info:fedora/!pid> ;
           <!sequence_predicate> $sequence_number .
}
ORDER BY DESC(?sequence_number)
LIMIT 1
EOQ;
  $formatted_query = format_string($sequence_query, array(
    '!parent_predicate' => "$fedora_namespace$intermediate_parent_predicate",
    '!pid' => $parent_id,
    '!sequence_predicate' => "$islandora_namespace$sequence_predicate",
  ));
  $sequence_number_results = $tuque->repository->ri->sparqlQuery($formatted_query);
  $sequence_number = reset($sequence_number_results);
  return $sequence_number ? $sequence_number['sequence_number']['value'] : 0;
}

/**
 * Determines whether or not the intermediate object is at the highest level of the serial tree.
 *
 * @param AbstractObject $parent_object
 *   An AbstractObject representing a Fedora object.
 *
 * @return bool
 *   TRUE if the intermediate is a leaf, FALSE otherwise.
 */
function islandora_serial_object_at_highest_level(AbstractObject $parent_object) {

  if (in_array('islandora:rootSerialCModel', $parent_object->models)) {
    return FALSE;
  }

  $tuque = islandora_get_tuque_connection();
  $query = <<<'EOQ'
SELECT ?subject ?content
FROM <#ri>
WHERE {
  ?subject <fedora-model:hasModel> ?content .
  <info:fedora/!pid> <fedora-rels-ext:isMemberOf>+ ?subject .
FILTER (sameTerm(?content, <info:fedora/islandora:rootSerialCModel>) || sameTerm(?content, <info:fedora/islandora:intermediateCModel>))
}
EOQ;
  $formatted_query = format_string($query, array(
    '!pid' => $parent_object->id,
  ));
  $results = $tuque->repository->ri->sparqlQuery($formatted_query);
  $level_counter = 0;
  $root_serial_pid = '';
  foreach ($results as $result) {
    $level_counter++;
    if ($result['content']['value'] == 'islandora:rootSerialCModel') {
      $root_serial_pid = str_replace('info:fedora/', '', $result['subject']['value']);
    }
  }
  $root_serial_object = islandora_object_load($root_serial_pid);
  $default_values = json_decode($root_serial_object['SERIAL-LEVELS']->content, TRUE);
  if ($level_counter == count($default_values)) {
    return TRUE;
  }
  return FALSE;
}
