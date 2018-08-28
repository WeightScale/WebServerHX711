var d = document;
function setOnBlur(ip) {
    setTimeout(function() {
        if (d.activeElement === ip) {
            ip.onblur = function() {
                if (ip.value.length === 0 || !CheckIP(ip)) {
                    setTimeout(function() {
                        ip.focus()
                    }, 0);
                } else ip.onblur = null;
            }
        }
    }, 0)
}

function CheckIP(ip) {
    ipParts = ip.value.split(".");
    if (ipParts.length === 4) {
        for (i = 0; i < 4; i++) {
            TheNum = parseInt(ipParts[i]);
            if (TheNum >= 0 && TheNum <= 255) {} else break;
        }
        if (i === 4) return true;
    }
    return false;
}

function sendDateTime() {
    var f = new FormData();
    var date = new Date();
    var dt = date.toLocaleDateString();
    dt += "-" + date.toLocaleTimeString();
    f.append('data', dt.replace(/[^\x20-\x7E]+/g, ''));
    var r = new XMLHttpRequest();
    r.onreadystatechange = function() {
        if (r.readyState === 4 && r.status === 200) {
            if (r.responseText !== null) {
                d.getElementById('id_date').innerHTML = "<div>Обновлено<br/>" + r.responseText + "</div>";
            }
        }
    };
    r.open("POST", "settings.html?" + new Date().getTime(), true);
    r.send(f);
}

function GetValue() {
    var r = new XMLHttpRequest();
    r.overrideMimeType('application/json');
    r.onreadystatechange = function() {
        if (r.readyState === 4) {
            if (r.status === 200) {
                var j = JSON.parse(r.responseText);
                for (e in j) {
                    try {
                        d.getElementById(e).innerHTML = j[e];
                    } catch (e) {}
                }
            }
        }
    };
    r.open('GET', '/sv', true);
    r.send(null);
}

function GetSettings() {
    var r = new XMLHttpRequest();
    r.overrideMimeType('application/json');
    r.onreadystatechange = function() {
        if (r.readyState === 4) {
            if (r.status === 200) {
                try {
                    var j = JSON.parse(r.responseText);
                    var s = j.scale;
                    for (e in s) {
                        try {
                            if (d.getElementById(e).type === 'checkbox') {
                                d.getElementById(e).checked = s[e];
                            } else d.getElementById(e).value = s[e];
                        } catch (e) {}
                    }
                    var sr = j.server;
                    for (en in sr) {
                        try {
                            d.getElementById(en).value = sr[en];
                        } catch (e) {}
                    }
                    enableAuthFields(document.getElementById('id_auto'));
                } catch (e) {
                    alert("ОШИБКА " + e.toString());
                }
            } else {
                alert("ДАННЫЕ НАСТРОЕК НЕ НАЙДЕНЫ!!!");
            }
            d.body.style.visibility = 'visible';
            GetValue();
        }
    };
    r.open('GET', '/settings.json', true);
    r.send(null);
}

function openSDB() {
    var url = 'https://' + d.getElementById('id_host').value + '/scale.php?code=' + d.getElementById('id_pin').value;
    var win = window.open(url, '_blank');
    win.focus();
}

function enableAuthFields(check) {
    if (check.checked) {
        d.getElementById('id_table_net').style.display = 'none';
    } else {
        d.getElementById('id_table_net').style.display = '';
    }
}

function formNet(i) {
    var f = new FormData(d.getElementById(i));
    var r = new XMLHttpRequest();
    r.onreadystatechange = function() {
        if (r.readyState === 4) {
            if (r.status === 200) {
                var rec = confirm('Перегрузить');
                if (rec) {
                    r.onreadystatechange = null;
                    r.open('GET', '/rc', true);
                    r.send(null);
                }
            } else if (r.status === 400) {
                alert('Настройки не изменились');
            }
        }
    };
    r.open('POST', '/settings.html', true);
    r.send(f);
}