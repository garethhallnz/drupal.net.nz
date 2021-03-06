<?php

/**
 * @file worker.inc
 *
 * Queue-processing worker-related functions.
 *
 * FIXME: these worker callbacks only differ in the method names they call and
 * the string in their error messages. Seems very wasteful, and really points to
 * needing better architecture.
 */

/**
 * Process a versioncontrol_repomgr queue job.
 *
 * @param type $data
 *   The array of job data to be processed.
 */
function _versioncontrol_repomgr_run_worker($data) {
  // Lots and LOTS of input validation; this is a really bad place to fail silently.
  if (empty($data['repository'])) {
    $str = "Repository management job payload contained no repository data.";
    watchdog('versioncontrol', $str, array(), WATCHDOG_ERROR);
    throw new Exception($str, E_ERROR);
  }
  else if ($data['repository'] instanceof VersioncontrolRepository) {
    // Full object was serialized, no loading required
    $repo = $data['repository'];
  }
  else {
    // Expect that a repo_id was sent and we load it out here. Ensure we bypass
    // cache, as caching in a long-running worker would be trouble.
    $repo = versioncontrol_repository_load($data['repository'], array(), array('may cache' => FALSE));
    if (!$repo instanceof VersioncontrolRepository) {
      $str = "Repository management job enqueued with repo_id '%repo_id', which failed to load a valid VersioncontrolRepository object.";
      $vars = array(
        '%repo_id' => $data['repository'],
      );
      watchdog('versioncontrol', $str, $vars, WATCHDOG_ERROR);
      throw new Exception(strtr($str, $vars), E_ERROR);
    }
  }

  $manager = $repo->getRepositoryManager();

  if (empty($data['operation'])) {
    watchdog('versioncontrol', "Repository management job was enqueued without any operations to run.", array(), WATCHDOG_ERROR);
    throw new Exception("Repository management job was enqueued without any operations to run.", E_ERROR);
  }

  $ops = is_string($data['operation']) ? array($data['operation'] => array()) : $data['operation'];

  // Loop through all ops and ensure all are valid before running any. This
  // helps preserve known state by preventing jobs containing ops that are
  // guaranteed to terminate partway through from starting at all.
  foreach (array_keys($ops) as $op) {
    if (!method_exists($manager, $op)) {
      $vars = array(
        '%op' => $op,
        '%repo_id' => empty($repo->repo_id) ? '[NEW]' : $repo->repo_id,
        '%plugin' => get_class($manager),
      );
      watchdog('versioncontrol', "Operation '%op' was requested on repo_id %repo_id using plugin class %plugin, but no such method exists.", $vars, WATCHDOG_ERROR);
      throw new Exception(strtr("Operation '%op' was requested on repo_id %repo_id using plugin class %plugin, but no such method exists.", $vars), E_ERROR);
    }
  }

  // Finally ready to actually run the operations.
  foreach ($ops as $op => $args) {
    call_user_func_array(array($manager, $op), $args);
  }
}

function _versioncontrol_reposync_run_worker($data) {
  // If a repo_id is provided, use it to try to load a repository object. This
  // will supercede any repository object provided directly.
  if (!empty($data['repo_id'])) {
    $data['repository'] = versioncontrol_repository_load($data['repo_id'], array(), array('may cache' => FALSE));
  }

  if (empty($data['repository'])) {
    $str = "Repository history synchronization job payload contained no repository data.";
    watchdog('versioncontrol', $str, array(), WATCHDOG_ERROR);
    throw new Exception($str, E_ERROR);
  }
  else if ($data['repository'] instanceof VersioncontrolRepository) {
    // Full object was serialized, no loading required
    $repository = $data['repository'];
  }
  else {
    // Expect that a repo_id was sent and we load it out here
    $repository = versioncontrol_repository_load($data['repository'], array(), array('may cache' => FALSE));
    if (!$repository instanceof VersioncontrolRepository) {
      $str = "Repository parsing job enqueued with repo_id '%repo_id', which failed to load a valid VersioncontrolRepository object.";
      $vars = array(
        '%repo_id' => $data['repository'],
      );
      watchdog('versioncontrol', $str, $vars, WATCHDOG_ERROR);
      throw new Exception(strtr($str, $vars), E_ERROR);
    }
  }

  $event = $repository->generateCodeArrivalEvent($data);
  // Insert the event right away, even though it may not yet be fully realized
  $event->save();

  if ($repository->syncEvent($event)) {
    // Fire the notification hook on success: new code is here, parsed and
    // ready to go!
    module_invoke_all('versioncontrol_code_arrival', $repository, $event);
  }
}
