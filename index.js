#!/usr/bin/node

const fs = require('fs');
const http = require('http');
const YAML = require('yaml');
const mysql = require('mysql');

const iTeeConfigFile = '/etc/i-tee/config.yaml';
const iTeeConfig = YAML.parse(fs.readFileSync(iTeeConfigFile).toString());


const machine = process.env['VBOX_MACHINE'];
const username = process.env['VBOX_USERNAME'];
const password = process.env['VBOX_PASSWORD'];


if(!machine || !username || !password) {
	console.error('Environment variables VBOX_MACHINE, VBOX_USERNAME and VBOX_PASSWORD must be provided');
	process.exit(1);
}


async function getMachineName(uuid) {
	return new Promise((resolve, reject) => {
		const client = http.request('http://localhost:3001/machine/'
			+ encodeURIComponent(machine) + '?access_token='
			+ encodeURIComponent('RYdtOU7XvZkVr1k1GEv0GIvCLPqlQyCC'), res => {
			if(res.statusCode < 200 || res.statusCode > 299) {
				reject(new Error('Bad status code: ' + res.statusCode));
			} else {
				const buffers = [];
				res.on('data', buffer => {
					buffers.push(buffer);
				});
				res.on('end', () => {
					try {
						resolve(JSON.parse(Buffer.concat(buffers)).id);
					} catch(e) {
						reject(e);
					}
				});
			}
		});
		client.once('error', reject);
		client.end();
	});
}


async function queryDatabase(username, password) {
	return new Promise((resolve, reject) => {
		const connection = mysql.createConnection({
			host     : iTeeConfig.database.host,
			user     : iTeeConfig.database.username,
			password : iTeeConfig.database.password,
			database : iTeeConfig.database.database
		});
		connection.once('error', reject);
		connection.connect();
		connection.query('SELECT COUNT(*) AS count FROM users where CONCAT(users.username, \'-admin\')=? AND users.role=2 AND users.rdp_password=?'
			+ ' UNION ALL '
			+ 'SELECT vms.name AS machine FROM users,lab_users,vms WHERE users.username=? AND lab_users.user_id=users.id AND vms.lab_user_id=lab_users.id AND vms.password = ?;'
			, [ username, password, username, password ], (e, results, fields) => {
			if(e) {
				reject(e);
			} else {
				if(results[0].count === '1') {
					resolve(true);
				} else {
					resolve(results.slice(1).map(v => v.machine));
				}
			}
		});
		connection.end();
	});
}


Promise.all([ getMachineName(machine), queryDatabase(username, password) ])
	.then(([ machine, queryResult ]) => {
		if(queryResult === true || queryResult.includes(machine)) {
			console.log('Y');
		} else {
			console.log('N');
		}
	});
