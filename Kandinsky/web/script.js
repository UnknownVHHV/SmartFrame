/* script.js */
let currentLang = 'RU'; 
let uuid, prd;

function toggleLanguage() {
    currentLang = currentLang === 'RU' ? 'EN' : 'RU'; 
    document.getElementById('language-button').textContent = currentLang;
    localStorage.setItem('language', currentLang);
    location.reload();
}

window.onload = () => {
    const storedLang = localStorage.getItem('language');
    if (storedLang) {
        currentLang = storedLang;
        document.getElementById('language-button').textContent = currentLang;
    }
    loadStyles();
};

async function loadStyles() {
    let res = await fetch('https://cdn.fusionbrain.ai/static/styles/web');
    res = await res.json();
    for (let style of res) {
        document.getElementById('style').innerHTML += `<option value="${style.name}">${style.name}</option>`;
    }
}

async function updateBrightness(value) {
    // Отправляем запрос к существующему веб-серверу SettingsGyver
    let formData = new FormData();
    formData.append('brightness', value);
    await fetch('/settings', { // Предполагаемый эндпоинт SettingsGyver
        method: 'POST',
        body: formData,
    });
}

async function generate() {
    let model_id = 0;
    {
        let res = await fetch('https://api-key.fusionbrain.ai/key/api/v1/models', {
            method: 'GET',
            headers: headers(),
        });
        res = await res.json();
        model_id = res[0].id;
    }

    let formData = new FormData();
    formData.append('model_id', model_id);
    formData.append('params', new Blob([JSON.stringify(params())], { type: "application/json" }));

    let res = await fetch('https://api-key.fusionbrain.ai/key/api/v1/text2image/run', {
        method: 'POST',
        headers: headers(),
        body: formData,
    });
    let json = await res.json();
    console.log(json);

    uuid = json.uuid;
    if (json.uuid) prd = setInterval(check, 3000);
}

async function check() {
    let res = await fetch('https://api-key.fusionbrain.ai/key/api/v1/text2image/status/' + uuid, {
        method: 'GET',
        headers: headers(),
    });
    let json = await res.json();
    console.log(json);

    switch (json.status) {
        case 'INITIAL':
        case 'PROCESSING':
            break;
        case 'DONE':
            document.getElementById('img').src = 'data:image/jpeg;charset=utf-8;base64,' + json.images[0];
            clearInterval(prd);
            break;
        case 'FAIL':
            clearInterval(prd);
            break;
    }
}

function headers() {
    return {
        'X-Key': 'Key ' + token.value,
        'X-Secret': 'Secret ' + secret.value,
    }
}

function params() {
    return {
        type: "GENERATE",
        style: style.value,
        width: width.value,
        height: height.value,
        num_images: 1,
        negativePromptUnclip: negative.value,
        generateParams: {
            query: query.value,
        }
    }
}